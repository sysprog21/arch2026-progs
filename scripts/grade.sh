#!/usr/bin/env bash

# Grade a submission with this repository's own harness.
#
#   scripts/grade.sh [--submission DIR] [--out DIR] [--timeout S] [--chain]
#                    [--secret DIR]
#
# The harness is everything but the implementations: tests/ (the test programs,
# the main program of each ELF, the vectors), common/, scripts/, mk/, the
# Makefile, and the src/<prog>/<prog>.h contracts (they hold the budgets). A
# submission supplies the rest of src/<prog>/: C and assembly sources, tables
# and headers of its own. Grading copies this repository into a scratch tree,
# replaces the sources of each program with the submission's, and runs
# sanitizers, the build and scripts/driver.py there, so editing a test, a
# budget, a vector or the driver in the submission changes nothing. The harness
# reaches a contract by its path, so a header of the submission cannot take the
# place of a harness or system header either. Run it from a checkout of
# sysprog21/arch2026-progs (the instructor's, or .trusted in CI), not from the
# submission.
#
# --chain first verify the Change-Id chain of the submission's history (the
#           fork needs full history, and src/ must be committed)
# --secret copy the files of DIR over tests/ first: the instructor's private
#           vectors (tools/oracle/gen.py --seed N --out DIR), which a student
#           cannot have memorized
#
# The default submission is the current directory. Run from a checkout, it
# grades that checkout with its own harness, which is what a student gets from
# `make grade`.

here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"
set_colors

trusted=$(cd "$here/.." && pwd)
sub=$(pwd)
out=results
timeout=120
chain=0
secret=
while [ $# -gt 0 ]; do
  case "$1" in
    --submission) sub=$(cd "$2" && pwd) || throw "No such directory: %s" "$2"; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --timeout) timeout=$2; shift 2 ;;
    --chain) chain=1; shift ;;
    --secret) secret=$(cd "$2" && pwd) || throw "No such directory: %s" "$2"; shift 2 ;;
    *) sed -n '3,30p' "$0"; exit 2 ;;
  esac
done
case "$out" in /*) ;; *) out="$sub/$out" ;; esac

PROGS="rubik bf16 gcd fixed"
for p in $PROGS; do
  [ -d "$sub/src/$p" ] || throw "The submission has no src/%s." "$p"
done

status=0

if [ $chain -eq 1 ]; then
  echo "== hash chain of the submission's history"
  (cd "$sub" && "$here/chain.sh" verify) || exit 1
  if [ -n "$(git -C "$sub" status --porcelain -- src 2>/dev/null)" ]; then
    throw "src/ has changes that are not committed: they would be graded but not verified."
  fi
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/grade.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
echo "== harness from $trusted"
tar -C "$trusted" -cf - --exclude=.git --exclude=build --exclude=third_party \
    --exclude=.trusted --exclude=results --exclude=tools/oracle --exclude=__pycache__ . |
  tar -C "$work" -xf -

echo "== sources from $sub"
for p in $PROGS; do
  # Only the submission's sources are built: the dummies of the harness go.
  find "$work/src/$p" -maxdepth 1 \( -name '*.c' -o -name '*.S' \) -delete
  tar -C "$sub/src/$p" -cf - --exclude="$p.h" --exclude=main.c . | tar -C "$work/src/$p" -xf -
done
# A link could read a file of the grader into the build.
find "$work/src" -type l -delete

if [ -n "$secret" ]; then
  echo "== private vectors from $secret"
  cp -R "$secret"/. "$work/tests/"
fi

# The emulator is the harness's too: built from this checkout's rules, never
# from the submission's, and kept between runs (a CI cache) in third_party/.
mkdir -p "$trusted/third_party"
ln -s "$trusted/third_party" "$work/third_party"

echo "== host tests under sanitizers"
make -C "$work" sanitize || status=1

echo "== build and grade"
make -C "$work" DRIVER_ARGS="--timeout $timeout --out $out" grade || status=1

exit $status
