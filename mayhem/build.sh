#!/usr/bin/env bash
#
# mayhem/build.sh — builds the fuzz_zlib_stream harness, its -standalone reproducer and the known-answer test.
#
# Upstream's own build is Bazel + V8, which cannot run air-gapped, so the fuzzed scope is workerd's node:zlib
# handle (src/workerd/api/node/zlib-util.c++ and src/workerd/api/compression.c++, compiled from the repo
# sources into one translation unit with the harness) over brotli 1.2.0 built from source with the fuzz
# flags. zlib and zstd are the image's apt libraries (uninstrumented), V8 is replaced by the stand-ins in
# mayhem/v8_stubs.c++, and the capnp headers the sources include are generated here. Everything comes from
# the pinned caches under /opt/toolchains/deps, so it re-runs offline; every output lives in mayhem/build/
# (gitignored) or /mayhem/<target>[-standalone].
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

DEPS=/opt/toolchains/deps
CP=$(echo "$DEPS"/capnproto-*)
V8=$(echo "$DEPS"/v8-headers-*)
NBYTES=$(echo "$DEPS"/nbytes-*)/src
BROTLI=$(echo "$DEPS"/brotli-*)/src
B="$SRC/mayhem/build"
GEN="$B/gen"
TARGET=fuzz_zlib_stream

INCLUDES="-I$SRC/src -I$SRC/mayhem -I$GEN -I$CP/include -I$CP/src/c++/src -I$V8/include -I$NBYTES/include -I$BROTLI/c/include"
# -fno-access-control: the harness initializes the handle through its protected/private members.
CXXSTD="-std=c++23 -DKJ_USE_FIBERS -fno-access-control"
LIBS="-L$CP/lib -lkj-http -lkj-async -lkj -lzstd -lz -lpthread -lm"

mkdir -p "$B/fuzz" "$B/test" "$B/tests-bin" "$GEN"

# 1) The capnp headers workerd's sources include (schemas from the repo and from capnproto's compat dir).
export PATH="$CP/bin:$PATH"
for f in "$CP/src/c++/src/capnp/compat/byte-stream.capnp" "$CP/src/c++/src/capnp/compat/http-over-capnp.capnp"; do
  capnp compile -I"$CP/include" -I"$CP/src/c++/src" --src-prefix="$CP/src/c++/src" -oc++:"$GEN" "$f"
done
find src/workerd -name '*.capnp' -not -path '*/tests/*' | sort | while read -r f; do
  capnp compile -I"$CP/include" -I"$CP/src/c++/src" -I src --src-prefix=src -oc++:"$GEN" "$f"
done

# 2) brotli: instrumented for the fuzz build, plain for the known-answer test.
BROTLI_SRCS=$(ls "$BROTLI"/c/common/*.c "$BROTLI"/c/dec/*.c "$BROTLI"/c/enc/*.c)
brotli_obj() { echo "$1/brotli_$(basename "$(dirname "$2")")_$(basename "$2").o"; }
export -f brotli_obj
for s in $BROTLI_SRCS; do
  echo "$B/fuzz $s $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
  echo "$B/test $s $COVERAGE_FLAGS -O1"
done | xargs -P "$MAYHEM_JOBS" -L1 bash -c '
  dir="$0"; src="$1"; shift
  '"$CC"' -I'"$BROTLI"'/c/include "$@" -c "$src" -o "$(brotli_obj "$dir" "$src")"'

# 3) The harness and stand-ins: the workerd sources are compiled into the harness translation unit.
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
"$CXX" $CXXSTD $INCLUDES $FUZZ_FLAGS -c "$SRC/mayhem/harness/$TARGET.c++" -o "$B/fuzz/$TARGET.o" &
"$CXX" $CXXSTD $INCLUDES $FUZZ_FLAGS -c "$SRC/mayhem/v8_stubs.c++" -o "$B/fuzz/v8_stubs.o" &
"$CXX" $CXXSTD $INCLUDES $COVERAGE_FLAGS -O1 -c "$SRC/mayhem/selftest/zlib_kat.c++" -o "$B/test/zlib_kat.o" &
"$CXX" $CXXSTD $INCLUDES $COVERAGE_FLAGS -O1 -c "$SRC/mayhem/v8_stubs.c++" -o "$B/test/v8_stubs.o" &
wait

# 4) lsan_off, the run-once driver (compiled as C so the harness reference keeps C linkage)
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

FUZZ_BROTLI=(); TEST_BROTLI=()
for s in $BROTLI_SRCS; do FUZZ_BROTLI+=("$(brotli_obj "$B/fuzz" "$s")"); TEST_BROTLI+=("$(brotli_obj "$B/test" "$s")"); done

"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz/$TARGET.o" "$B/fuzz/v8_stubs.o" \
  "${FUZZ_BROTLI[@]}" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$TARGET"
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$B/fuzz/$TARGET.o" "$B/fuzz/v8_stubs.o" "$B/fuzz/standalone_main.o" \
  "${FUZZ_BROTLI[@]}" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$TARGET-standalone"

# 5) The known-answer test, normal flags, no sanitizers.
"$CXX" $COVERAGE_FLAGS "$B/test/zlib_kat.o" "$B/test/v8_stubs.o" "${TEST_BROTLI[@]}" $LIBS \
  -o "$B/tests-bin/zlib-kat"
