#!/usr/bin/env bash
#
# mayhem/test.sh — runs workerd's own kj-test suites (src/workerd/{jsg/url,util/mimetype,util/uuid}-test.c++)
# that mayhem/build.sh compiled against the same sources the harnesses fuzz. Each case asserts concrete
# known-input -> known-output values (WHATWG URL parsing, MIME parsing, UUID round trips).
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN="$SRC/mayhem/build/tests-bin"
# <binary> <minimum number of PASSing cases it must report>; the floor stops a stubbed binary that
# exits 0 without running anything from counting as a pass.
SUITES="url-test:50 mimetype-test:7 uuid-test:3"

passed=0 ; failed=0
for s in $SUITES; do
  name="${s%%:*}" ; min="${s##*:}"
  if [ ! -x "$BIN/$name" ]; then echo "missing test binary $BIN/$name (build.sh bug)" >&2; failed=$((failed + 1)); continue; fi
  out=$("$BIN/$name" 2>&1) ; rc=$?
  printf '%s\n' "$out" | tail -n 3
  p=$(printf '%s\n' "$out" | grep -c '^\[ PASS \]') || true
  f=$(printf '%s\n' "$out" | grep -c '^\[ FAIL \]') || true
  if [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then f=1; fi
  if [ "$p" -lt "$min" ]; then echo "$name: only $p passing cases, expected >= $min" >&2; f=$((f + 1)); fi
  passed=$((passed + p)) ; failed=$((failed + f))
done

emit_ctrf "workerd-kj-test" "$passed" "$failed" 0
