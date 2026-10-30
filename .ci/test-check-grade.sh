#!/usr/bin/env bash
# Exercise checker decisions without private submissions or a cross toolchain.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/reference/cases/negative" "$tmp/skeleton"
cat > "$tmp/grade" <<'EOF'
#!/usr/bin/env bash
echo 'private grader output must stay hidden'
if [ -s "$2/total" ]; then
  printf -- '--- TOTAL %s/100\n' "$(cat "$2/total")"
fi
exit "$(cat "$2/status")"
EOF
chmod +x "$tmp/grade"
printf '100.0\n' > "$tmp/reference/total"
printf '0\n' > "$tmp/reference/status"
printf '0.0\n' > "$tmp/skeleton/total"
printf '1\n' > "$tmp/skeleton/status"
printf '25.0\n' > "$tmp/reference/cases/negative/total"
printf '25.0\n' > "$tmp/reference/cases/negative/expect"
printf '1\n' > "$tmp/reference/cases/negative/status"

check() {
  local status=0
  bash "$here/check-grade.sh" "$tmp/grade" "$tmp/reference" "$tmp/skeleton" \
    > "$tmp/log" 2>&1 || status=$?
  [ "$status" -eq "$1" ] || { cat "$tmp/log"; exit 1; }
  if grep -q 'private grader output' "$tmp/log"; then
    echo 'checker leaked grader output' >&2
    exit 1
  fi
}

check 0
printf '1\n' > "$tmp/reference/status"
check 1
printf '0\n' > "$tmp/reference/status"
printf '99.0\n' > "$tmp/reference/total"
check 1
printf '100.0\n' > "$tmp/reference/total"
printf '0.0\n' > "$tmp/reference/cases/negative/total"
check 1
: > "$tmp/reference/cases/negative/total"
check 1
echo 'check-grade tests passed'
