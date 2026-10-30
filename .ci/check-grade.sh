#!/usr/bin/env bash
# Check the grader itself: the reference solutions must score 100 and every
# negative case of the private repository must score what its "expect" file
# says, so that a harness change which lets a cheating submission through, or
# turns away a correct one, fails CI.
#
#   .ci/check-grade.sh GRADE_SH DIR [SKELETON]
#
# GRADE_SH is the scripts/grade.sh to test. DIR is a tree made by
# .ci/provision.sh: src/ holds the reference solutions and cases/<name>/ holds
# a submission (src/<prog>/...) next to a file "expect" with the total it must
# score. SKELETON, when given, is a checkout whose untouched src/ must score 0.
#
# Only the name and the total of each case are printed, never a log: the cases
# are private.

set -uo pipefail

grade=${1:?usage: check-grade.sh GRADE_SH DIR [SKELETON]}
dir=${2:?usage: check-grade.sh GRADE_SH DIR [SKELETON]}
skeleton=${3:-}

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
failed=0

# check NAME SUBMISSION EXPECTED_TOTAL
check() {
  local out got status=0
  out=$("$grade" --submission "$2" --out "$tmp/out" 2>&1) || status=$?
  got=$(sed -n 's/^--- TOTAL \([0-9.]*\)\/.*/\1/p' <<< "$out")
  if [ "$got" = "$3" ] && { [ "$1" != reference ] || [ "$status" -eq 0 ]; }; then
    echo "check-grade: $1: $got, as expected"
  else
    echo "check-grade: $1: expected $3, got ${got:-no total} (grader exit $status)" >&2
    failed=1
  fi
}

check reference "$dir" 100.0
[ -z "$skeleton" ] || check skeleton "$skeleton" 0.0
for c in "$dir"/cases/*/; do
  [ -f "$c/expect" ] || continue
  check "case $(basename "$c")" "$c" "$(tr -d '[:space:]' < "$c/expect")"
done
exit $failed
