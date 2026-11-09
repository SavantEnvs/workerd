#!/usr/bin/env bash
#
# mayhem/build.sh — builds the workerd fuzz harnesses, their -standalone reproducers and the kj-test oracle.
#
# Upstream's own build is Bazel + V8, which cannot run air-gapped, so the fuzzed scope is the V8-free part
# of workerd (jsg/url.c++, util/{mimetype,uuid,sentry,strings,thread-scopes}.c++), compiled from the repo
# sources against the pinned static libraries the Dockerfile installs under /opt/toolchains/deps.
# Re-runnable offline; every output lives in mayhem/build/ (gitignored) or /mayhem/<target>[-standalone].
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
ADA=$(echo "$DEPS"/ada-*)
B="$SRC/mayhem/build"
W="$SRC/src/workerd"

INCLUDES="-I$SRC/src -I$CP/include -I$V8/include -I$ADA/include"
CXXSTD="-std=c++23 -DKJ_USE_FIBERS"
LIBS="-L$CP/lib -lkj-http -lkj-async -lkj -L$ADA/lib -lada -licuuc -licudata -lpthread -lm"

LIB_SRCS=(
  "$W/jsg/url.c++"
  "$W/util/sentry.c++"
  "$W/util/strings.c++"
  "$W/util/mimetype.c++"
  "$W/util/uuid.c++"
  "$W/util/thread-scopes.c++"
  "$SRC/mayhem/entropy_stub.c++"
)
TARGETS=(fuzz_url fuzz_urlpattern fuzz_mimetype fuzz_uuid)
TESTS=(jsg/url-test util/mimetype-test util/uuid-test)

mkdir -p "$B/fuzz" "$B/test" "$B/tests-bin"

# objname <dir> <source> -> object path
objname() { echo "$1/$(echo "${2#$SRC/}" | tr '/+' '__').o"; }

compile_one() {  # <kind: fuzz|test> <source>
  local kind="$1" src="$2" out flags
  if [ "$kind" = fuzz ]; then
    out=$(objname "$B/fuzz" "$src")
    flags="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
  else
    out=$(objname "$B/test" "$src")
    flags="-O1 $COVERAGE_FLAGS"
  fi
  "$CXX" $CXXSTD $INCLUDES $flags -c "$src" -o "$out"
}
export -f compile_one objname
export SRC B CXX CXXSTD INCLUDES

# 1) fuzzed workerd sources (instrumented) + harnesses
{
  for s in "${LIB_SRCS[@]}"; do echo "fuzz $s"; done
  for t in "${TARGETS[@]}"; do echo "fuzz $SRC/mayhem/harness/$t.c++"; done
} | xargs -P "$MAYHEM_JOBS" -L1 bash -c 'compile_one "$0" "$1"'

# 2) lsan_off, the run-once driver (compiled as C so the harness reference keeps C linkage)
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/fuzz/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/fuzz/standalone_main.o"

LIB_OBJS=()
for s in "${LIB_SRCS[@]}"; do LIB_OBJS+=("$(objname "$B/fuzz" "$s")"); done

for t in "${TARGETS[@]}"; do
  h=$(objname "$B/fuzz" "$SRC/mayhem/harness/$t.c++")
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$h" "${LIB_OBJS[@]}" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$t"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$h" "$B/fuzz/standalone_main.o" "${LIB_OBJS[@]}" "$B/fuzz/lsan_off.o" $LIBS -o "$SRC/$t-standalone"
done

# 3) workerd's own kj-test suites for the same sources, normal flags, no sanitizers
{
  for s in "${LIB_SRCS[@]}"; do echo "test $s"; done
  for t in "${TESTS[@]}"; do echo "test $W/$t.c++"; done
} | xargs -P "$MAYHEM_JOBS" -L1 bash -c 'compile_one "$0" "$1"'

TEST_LIB_OBJS=()
for s in "${LIB_SRCS[@]}"; do TEST_LIB_OBJS+=("$(objname "$B/test" "$s")"); done
for t in "${TESTS[@]}"; do
  "$CXX" -O1 $COVERAGE_FLAGS "$(objname "$B/test" "$W/$t.c++")" "${TEST_LIB_OBJS[@]}" \
    -L"$CP/lib" -lkj-test -lkj-http -lkj-async -lkj -L"$ADA/lib" -lada -licuuc -licudata -lpthread -lm \
    -o "$B/tests-bin/$(basename "$t")"
done
