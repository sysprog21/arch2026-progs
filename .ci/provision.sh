#!/usr/bin/env bash
# Build a tree from the private repository, for the instructor's CI: the
# reference solutions as a submission, and the negative cases that check the
# grader (see check-grade.sh).
#
#   SSH_PRIVATE_KEY=... .ci/provision.sh DEST
#
# DEST/src/<prog>/ receives the .c, .S and .h files of the same directory of
# the private repository, a fresh clone every time; nothing of the caller's
# working tree is mixed in. DEST/cases/<name>/ receives every regular file of
# cases/<name>/, a submission tree next to the file "expect".
#
# SSH_PRIVATE_KEY is a read-only deploy key. Without it the script prints a
# notice and creates nothing, as in a fork. With it, a missing program is an
# error.
#
# Run this script from a trusted checkout only, never from a pull request.

set -euo pipefail

REPO=git@github.com:sysprog21/arch2026-progs-private.git
PROGS="rubik bf16 gcd fixed"
# GitHub's ed25519 host key, whose fingerprint is
# SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU, see
# https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/githubs-ssh-key-fingerprints
GITHUB_HOST='github.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIOMqqnkVzrm0SdG6UOoqKLsabgH5C9okWi0dh2l9GKJl'

dest=${1:?usage: provision.sh DEST}

if [ -z "${SSH_PRIVATE_KEY:-}" ]; then
  echo "provision: no provisioning profile found; nothing provisioned"
  exit 0
fi

umask 077
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

printf '%s\n' "$SSH_PRIVATE_KEY" > "$tmp/key"
printf '%s\n' "$GITHUB_HOST" > "$tmp/hosts"
unset SSH_PRIVATE_KEY

GIT_SSH_COMMAND="ssh -F /dev/null -i '$tmp/key' -o IdentitiesOnly=yes -o IdentityAgent=none -o BatchMode=yes -o UserKnownHostsFile='$tmp/hosts' -o StrictHostKeyChecking=yes" \
  git clone --quiet --depth 1 --branch main "$REPO" "$tmp/ref"
rm -f "$tmp/key"

for prog in $PROGS; do
  src="$tmp/ref/src/$prog"
  [ -f "$src/$prog.c" ] && [ -f "$src/$prog.S" ] || {
    echo "provision: the private repository has no src/$prog/$prog.c and $prog.S" >&2
    exit 1
  }
  mkdir -p "$dest/src/$prog"
  find "$src" -maxdepth 1 -type f \( -name '*.c' -o -name '*.S' -o -name '*.h' \) \
    -exec cp -- {} "$dest/src/$prog/" \;
  echo "provision: src/$prog"
done

for c in "$tmp"/ref/cases/*/; do
  [ -d "$c" ] || continue
  name=$(basename "$c")
  while IFS= read -r -d '' f; do
    mkdir -p "$dest/cases/$name/$(dirname "$f")"
    cp -- "$c/$f" "$dest/cases/$name/$f"
  done < <(cd "$c" && find . -type f -print0)
  echo "provision: cases/$name"
done
