#!/usr/bin/env bash

# The hash chain of Change-Ids (see tools/hashchain.c).
#
#   chain.sh verify [TIP] check the chain from the base up to TIP (default HEAD)
#   chain.sh id [OLD]   print the Change-Id digits for the commit being made
#                       (the commit-msg hook calls this; OLD is the Change-Id
#                       the message carried before, which hints at an amend)
#   chain.sh fix        after a commit: restamp HEAD if its link is wrong
#                       (the post-commit hook calls this; it covers every
#                       case id guessed wrong, such as an amend with -m)
#   chain.sh stamp      rewrite the Change-Id of HEAD to continue its parent
#   chain.sh rechain    restamp the commits since the base commit whose link
#                       is wrong, after a rebase or a squash; the commits it
#                       rewrites get a "Rechained: yes" trailer
#   chain.sh base       print the base commit
#   chain.sh selftest   exercise all of the above in a scratch repository
#
# History must be linear: no merge commits since the base commit. Rebase onto
# the upstream branch instead of merging it, then run `chain.sh rechain`.

BASE="${CHAIN_BASE:-75c966c27551ff77931c8b1559b672a5b27490cf}"

here=$(cd "$(dirname "$0")" && pwd)
source "$here/common.sh"
set_colors

root=$(git rev-parse --show-toplevel 2>/dev/null) || throw "This is not a Git repository."
cd "$root" || exit 1
HC="${CHAIN_TOOL:-$root/build/hashchain}"

build_tool() {
  if [ "$1" = trusted ]; then
    # Verification must never execute a binary supplied by the submission.
    tool_tmp=$(mktemp -d) || throw "Cannot create a verifier build directory."
    trap 'rm -rf "$tool_tmp"' EXIT
    HC="$tool_tmp/hashchain"
    ${CC:-cc} -O2 -o "$HC" "$here/../tools/hashchain.c" || throw "Cannot build $HC."
    return
  fi
  [ -n "$CHAIN_TOOL" ] && return 0
  if [ ! -x "$HC" ] || [ "$here/../tools/hashchain.c" -nt "$HC" ]; then
    mkdir -p "$root/build"
    ${CC:-cc} -O2 -o "$HC" "$here/../tools/hashchain.c" || throw "Cannot build $HC."
  fi
}

# The Change-Id digits of a commit; the seed for the base commit.
link() {
  local sha id
  sha=$(git rev-parse --verify -q "$1^{commit}") || return 1
  if [ "$sha" = "$BASE" ]; then
    "$HC" seed "$BASE"
    return
  fi
  id=$(git log -1 --format='%(trailers:key=Change-Id,valueonly,separator=%x2C)' "$sha" | head -n1 | tr -d '\r')
  if [[ $id =~ ^I([0-9a-f]{40})$ ]]; then
    echo "${BASH_REMATCH[1]}"
    return
  fi
  echo "commit ${sha:0:12} has no valid Change-Id; run scripts/chain.sh rechain" >&2
  return 1
}

cmd_id() {
  local old=${1#I} head headlink prev tree
  build_tool
  head=$(git rev-parse --verify -q HEAD) || throw "There is no commit to continue the chain from."
  prev=
  if [ -n "$old" ] && [ "$head" != "$BASE" ]; then

    # The message still carries the Change-Id of HEAD: an amend of HEAD, which
    # the new commit replaces, so it continues from HEAD's parent. Other amends
    # (-m, -F) are repaired afterwards by `fix`.
    headlink=$(link HEAD 2>/dev/null)
    [ "$old" = "$headlink" ] && prev=$(link HEAD^)
  fi
  [ -n "$prev" ] || prev=$(link HEAD) || exit 1
  tree=$(git write-tree) || exit 1
  "$HC" next "$prev" "$tree"
}

cmd_verify() {
  local lines count bad sha subject restamped tip
  tip=$(git rev-parse --verify -q "${1:-HEAD}^{commit}") || throw "Invalid commit: %s" "${1:-HEAD}"
  build_tool trusted
  "$HC" selftest || throw "The hash implementation failed its self-test."
  git cat-file -e "$BASE^{commit}" 2>/dev/null ||
    throw "The base commit $BASE is missing. Fetch the full history: git fetch --unshallow"
  git merge-base --is-ancestor "$BASE" "$tip" ||
    throw "Commit $tip is not derived from the base commit $BASE.\nCreate your repository as a fork of sysprog21/arch2026-progs."
  if [ -n "$(git rev-list --merges "$BASE..$tip")" ]; then
    throw "Merge commits are not allowed since the base commit.\nRebase onto the upstream branch instead (git pull --rebase), then run scripts/chain.sh rechain."
  fi
  count=$(git rev-list --count "$BASE..$tip") || exit 1
  if [ "$count" -eq 0 ]; then
    echo -e "${GREEN}Commit history: no commits since the base commit.${NC}"
    return 0
  fi
  lines=$(git log --reverse --format='%H %T %(trailers:key=Change-Id,valueonly,separator=%x2C)' "$BASE..$tip" | tr -d '\r') ||
    throw "Cannot read the commit history."
  if [ "$(echo "$lines" | wc -l | tr -d ' ')" -ne "$count" ]; then
    throw "A Change-Id trailer spans several lines in one of the last $count commits.\nUse the form 'Change-Id: I<40 hex digits>' on a single line."
  fi
  if bad=$(echo "$lines" | cut -d' ' -f2- | "$HC" verify "$BASE" "$count" 2>&1); then
    restamped=$(git log --format='%(trailers:key=Rechained,valueonly)' "$BASE..$tip" | grep -c .)
    echo -e "${GREEN}Commit history: $count commit(s) continue the hash chain.${NC}"
    [ "$restamped" -eq 0 ] ||
      echo -e "${YELLOW}$restamped of them were restamped by 'scripts/chain.sh rechain'.${NC}"
    return 0
  fi
  bad=${bad#commit }
  bad=${bad%%:*}
  if [[ $bad =~ ^[0-9]+$ ]] && [ "$bad" -le "$count" ]; then
    sha=$(echo "$lines" | sed -n "${bad}p" | cut -d' ' -f1)
    subject=$(git log -1 --format=%s "$sha")
    throw "The Change-Id chain breaks at commit %s: %s\nIt was probably made with --no-verify, or history was rewritten.\nInstall the hooks with 'make', then repair the chain: scripts/chain.sh rechain" "${sha:0:12}" "$subject"
  fi
  throw "The Change-Id chain is malformed. Repair it: scripts/chain.sh rechain"
}

# Message of HEAD with its Change-Id replaced; empty commits are kept.
restamp_head() {
  local id=$1 reason=$2
  git log -1 --format=%B | grep -vi '^Change-Id:' | grep -vi '^Rechained:' |
    git stripspace |
    git interpret-trailers --trailer "Change-Id: I$id" ${reason:+--trailer "Rechained: $reason"} |
    git commit --amend --only --no-verify --allow-empty -q -F - || exit 1
}

# Digits that HEAD's Change-Id should have; fails when HEAD has no valid parent
# link.
expected_for_head() {
  local prev
  prev=$(link HEAD^) || return 1
  "$HC" next "$prev" "$(git rev-parse 'HEAD^{tree}')"
}

cmd_stamp() {
  local want have
  build_tool
  want=$(expected_for_head) || exit 1
  have=$(link HEAD 2>/dev/null)

  # A commit that already continues the chain is left alone, so rebasing
  # upstream commits keeps their ids and hashes.
  [ "$want" = "$have" ] && return 0
  restamp_head "$want" yes
}

# post-commit: repair the wrong Change-Id of the commit just made. Never fails
# the commit; a problem is reported and `verify` will report it again.
cmd_fix() {
  local want have
  [ -n "$CHAIN_FIXING" ] && return 0
  [ "$(git rev-parse --verify -q HEAD)" = "$BASE" ] && return 0
  git merge-base --is-ancestor "$BASE" HEAD 2>/dev/null || return 0
  [ "$(git rev-list --parents -n 1 HEAD | wc -w | tr -d ' ')" -eq 2 ] || return 0

  # Only a commit that went through commit-msg is repaired. One without any
  # Change-Id skipped the hooks (git commit --no-verify) and stays broken, so
  # that `verify` reports it; post-commit runs even under --no-verify.
  git log -1 --format='%(trailers:key=Change-Id,valueonly)' | grep -q . || return 0
  build_tool
  want=$(expected_for_head 2>/dev/null) || {
    echo -e "${YELLOW}The previous commit has no valid Change-Id: run scripts/chain.sh rechain${NC}" >&2
    return 0
  }
  have=$(link HEAD 2>/dev/null)
  [ "$want" = "$have" ] && return 0
  CHAIN_FIXING=1 restamp_head "$want" ""
  echo -e "${YELLOW}The Change-Id of the new commit was restamped to continue the chain.${NC}" >&2
}

cmd_rechain() {
  git rebase --autostash --exec "'$here/chain.sh' stamp" "${1:-$BASE}"
}

cmd_selftest() {
  local tool tmp fail
  CC=${CC:-cc}
  tmp=$(mktemp -d) || exit 1
  tool="$tmp/hashchain"
  $CC -O2 -o "$tool" "$here/../tools/hashchain.c" || exit 1
  (
    export CHAIN_TOOL="$tool" GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t \
           GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
    set -e
    git init -q "$tmp/r"
    cd "$tmp/r"
    git config core.hooksPath /dev/null
    git config commit.gpgsign false
    echo base > f && git add f && git commit -q -m "base"
    CHAIN_BASE=$(git rev-parse HEAD)
    export CHAIN_BASE
    chain() { "$here/chain.sh" "$@"; }
    # commit FILE-CONTENT SUBJECT: what the commit-msg hook does
    commit() {
      echo "$1" > "f$1" && git add "f$1"
      id=$(chain id)
      git commit -q -m "$2" -m "Change-Id: I$id"
    }
    expect_ok() { chain verify >/dev/null 2>&1 || { echo "FAIL (should pass): $1"; exit 1; }; echo "ok   $1"; }
    expect_bad() { ! chain verify >/dev/null 2>&1 || { echo "FAIL (should fail): $1"; exit 1; }; echo "ok   $1"; }

    expect_ok "no commits since the base"
    commit one "First"; commit two "Second"; commit three "Third"
    expect_ok "three chained commits"

    # amend: the message keeps HEAD's Change-Id, the content changes
    echo changed > fthree && git add fthree
    id=$(chain id "I$(git log -1 --format='%(trailers:key=Change-Id,valueonly)' | head -n1 | sed 's/^I//')")
    git commit -q --amend -m "Third, amended" -m "Change-Id: I$id"
    expect_ok "amend of HEAD"

    # a commit made with --no-verify has no Change-Id
    echo four > ffour && git add ffour && git commit -q -m "Fourth" --no-verify
    expect_bad "commit without Change-Id"

    valid=$(git rev-parse HEAD^)
    invalid=$(git rev-parse HEAD)
    chain verify "$valid" >/dev/null
    echo "ok   explicit valid tip with invalid checkout"
    mkdir -p build
    printf '#!/bin/sh\nexit 0\n' > build/hashchain
    chmod +x build/hashchain
    touch -t 209901010000 build/hashchain
    if (unset CHAIN_TOOL; chain verify >/dev/null 2>&1); then
      echo "FAIL: verifier trusted a cached submission binary"; exit 1
    fi
    if CHAIN_TOOL="$PWD/build/hashchain" chain verify >/dev/null 2>&1; then
      echo "FAIL: verifier trusted a submission binary"; exit 1
    fi
    echo "ok   verification ignores cached and overridden binaries"

    ln -s "$here" scripts
    cp "$here/pre-push.hook" .git/hooks/pre-push
    git checkout -q -b push-test "$valid"
    push_tip() {
      printf 'refs/heads/test %s refs/heads/test %040d\n' "$1" 0 |
        bash .git/hooks/pre-push >/dev/null 2>&1
    }
    if push_tip "$invalid"; then
      echo "FAIL: push accepted an invalid tip from a valid checkout"; exit 1
    fi
    push_tip "$valid"
    push_tip 0000000000000000000000000000000000000000
    if printf 'refs/heads/a %s refs/heads/a %040d\nrefs/heads/b %s refs/heads/b %040d\n' \
        "$valid" 0 "$invalid" 0 | bash .git/hooks/pre-push >/dev/null 2>&1; then
      echo "FAIL: push did not check every tip"; exit 1
    fi
    git checkout -q --detach "$invalid"
    push_tip "$valid"
    rm scripts .git/hooks/pre-push
    echo "ok   push checks every non-deleted tip independently of checkout"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain repairs a missing Change-Id"

    # a Change-Id copied from another commit does not fit the tree
    echo five > ffive && git add ffive
    git commit -q -m "Fifth" -m "$(git log -1 --format=%B | grep '^Change-Id')"
    expect_bad "copied Change-Id"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain repairs a copied Change-Id"

    # an amend with a new message gives the hook no old Change-Id to go by; the
    # post-commit step (fix) repairs the guess
    echo more >> ffive && git add ffive
    git commit -q --amend -m "Fifth, reworded" -m "Change-Id: I$(chain id)"
    expect_bad "amend with a new message, before fix"
    chain fix >/dev/null 2>&1
    expect_ok "fix repairs an amend with a new message"

    # A message-only amend must leave unrelated staged and unstaged edits alone.
    echo staged >> fone && git add fone
    echo unstaged >> fone
    tree=$(git rev-parse 'HEAD^{tree}')
    index=$(git write-tree)
    ln -s "$here" scripts
    cp "$here/post-commit.hook" .git/hooks/post-commit
    git config core.hooksPath .git/hooks
    git commit -q --amend --only -m "Fifth, reworded again" -m "Change-Id: I$(chain id)"
    git config core.hooksPath /dev/null
    rm scripts .git/hooks/post-commit
    expect_ok "fix repairs a message-only amend with staged changes"
    [ "$(git rev-parse 'HEAD^{tree}')" = "$tree" ] &&
      [ "$(git write-tree)" = "$index" ] &&
      [ "$(tail -n 1 fone)" = unstaged ] || { echo "FAIL: fix changed the tree, index or worktree"; exit 1; }
    git reset -q --hard HEAD

    # post-commit runs even under --no-verify, but must not launder it
    echo skip > fskip && git add fskip && git commit -q -m "Skipped" --no-verify
    chain fix >/dev/null 2>&1
    expect_bad "fix does not repair a commit without Change-Id"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain after a skipped commit"

    # rechain works around uncommitted changes
    echo wip > fwip && git add fwip && git commit -q -m "Skipped again" --no-verify
    echo "dirty" >> fone; echo staged > fstaged && git add fstaged
    chain rechain >/dev/null 2>&1
    expect_ok "rechain with uncommitted changes"
    [ "$(tail -n 1 fone)" = dirty ] && [ -f fstaged ] || { echo "FAIL: rechain lost the uncommitted changes"; exit 1; }
    echo "ok   uncommitted changes are kept"
    git checkout -q fone && git rm -q -f --cached fstaged && rm -f fstaged

    # an intentionally empty commit survives rechain
    git commit -q --allow-empty -m "Empty" --no-verify
    expect_bad "empty commit without Change-Id"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain keeps an empty commit"

    # two trailers, and text after the id
    git commit -q --amend --allow-empty -m "Twice" -m "Change-Id: Iaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" -m "Change-Id: Ibbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb" --no-verify
    expect_bad "two Change-Id trailers"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain strips both trailers"
    git commit -q --amend --allow-empty -m "Junk" -m "Change-Id: I$(chain id) junk" --no-verify
    expect_bad "text after the Change-Id"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain after trailing text"

    # rechain leaves commits that already fit the chain untouched, and marks the
    # ones it rewrote
    first=$(git rev-parse HEAD~5)
    echo six > fsix && git add fsix && git commit -q -m "Sixth" --no-verify
    chain rechain >/dev/null 2>&1
    [ "$(git rev-parse HEAD~6)" = "$first" ] || { echo "FAIL: rechain rewrote a valid commit"; exit 1; }
    echo "ok   rechain keeps valid commits"
    chain verify 2>&1 | grep -q "restamped by" || { echo "FAIL: restamped commits are not reported"; exit 1; }
    echo "ok   restamped commits are reported"

    # dropping a commit in the middle breaks every later link
    git rebase -q --onto HEAD~3 HEAD~2 >/dev/null 2>&1
    expect_bad "history rewritten by a rebase"
    chain rechain >/dev/null 2>&1
    expect_ok "rechain after a rebase"

    # merge commits
    git checkout -q -b side HEAD~1
    echo side > g && git add g && git commit -q -m "Side" --no-verify
    git checkout -q -
    git merge -q --no-ff side -m "Merge side" >/dev/null 2>&1
    expect_bad "merge commit"
    git reset -q --hard HEAD~1

    # a repository that does not contain the base commit
    git checkout -q --orphan other && git commit -q -m "Elsewhere" --allow-empty
    expect_bad "history not derived from the base"
  )
  fail=$?
  rm -rf "$tmp"
  [ $fail -eq 0 ] && echo -e "${GREEN}chain.sh selftest passed.${NC}"
  return $fail
}

case "$1" in
  verify)   cmd_verify "$2" ;;
  id)       cmd_id "$2" ;;
  fix)      cmd_fix ;;
  base)     echo "$BASE" ;;
  stamp)    cmd_stamp ;;
  rechain)  shift; cmd_rechain "$@" ;;
  selftest) cmd_selftest ;;
  *) sed -n '3,/^$/p' "$0"; exit 2 ;;
esac
