#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHELL_BIN="$ROOT_DIR/build/shell"

pass_count=0
fail_count=0

print_result() {
  local status="$1"
  local name="$2"
  if [[ "$status" == "PASS" ]]; then
    pass_count=$((pass_count + 1))
    echo "[PASS] $name"
  else
    fail_count=$((fail_count + 1))
    echo "[FAIL] $name"
  fi
}

build_shell() {
  cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" >/dev/null
  cmake --build "$ROOT_DIR/build" >/dev/null
}

run_shell_case() {
  local input="$1"
  local histfile="$2"
  HISTFILE="$histfile" "$SHELL_BIN" <<<"$input"
}

assert_contains() {
  local text="$1"
  local needle="$2"
  if grep -Fq "$needle" <<<"$text"; then
    return 0
  fi
  return 1
}

assert_regex() {
  local text="$1"
  local pattern="$2"
  if grep -Eq "$pattern" <<<"$text"; then
    return 0
  fi
  return 1
}

main() {
  build_shell

  local tmpdir
  tmpdir="$(mktemp -d)"
  trap "rm -rf '$tmpdir'" EXIT

  local histfile
  histfile="$tmpdir/history.txt"

  # 1) builtin + external dispatch
  local out1
  out1="$(run_shell_case $'echo hello\nexit' "$histfile")"
  if assert_contains "$out1" "hello"; then
    print_result "PASS" "echo builtin"
  else
    print_result "FAIL" "echo builtin"
    echo "$out1"
  fi

  # 2) cd ~ should resolve to HOME and pwd should print HOME
  local out2
  out2="$(run_shell_case $'cd /\ncd ~\npwd\nexit' "$histfile")"
  if assert_contains "$out2" "$HOME"; then
    print_result "PASS" "cd ~ + pwd"
  else
    print_result "FAIL" "cd ~ + pwd"
    echo "$out2"
  fi

  # 3) prompt should abbreviate HOME prefix to ~
  local out3
  out3="$(run_shell_case $'cd ~\nexit' "$histfile")"
  if assert_regex "$out3" '~\$ '; then
    print_result "PASS" "prompt uses ~ for HOME"
  else
    print_result "FAIL" "prompt uses ~ for HOME"
    echo "$out3"
  fi

  # 4) stdout redirection
  local out_file
  out_file="$tmpdir/redir.txt"
  local redir_case
  printf -v redir_case 'echo redirected > %s\nexit' "$out_file"
  run_shell_case "$redir_case" "$histfile" >/dev/null
  if [[ -f "$out_file" ]] && grep -Fxq "redirected" "$out_file"; then
    print_result "PASS" "stdout redirection"
  else
    print_result "FAIL" "stdout redirection"
    [[ -f "$out_file" ]] && cat "$out_file"
  fi

  # 5) simple pipeline
  local out5
  out5="$(run_shell_case $'echo pipeline | cat\nexit' "$histfile")"
  if assert_contains "$out5" "pipeline"; then
    print_result "PASS" "pipeline echo|cat"
  else
    print_result "FAIL" "pipeline echo|cat"
    echo "$out5"
  fi

  # 6) history writes and loads via HISTFILE on exit/start
  run_shell_case $'echo history-one\nexit' "$histfile" >/dev/null
  local out6
  out6="$(run_shell_case $'history\nexit' "$histfile")"
  if assert_contains "$out6" "history-one"; then
    print_result "PASS" "history persist via HISTFILE"
  else
    print_result "FAIL" "history persist via HISTFILE"
    echo "$out6"
  fi

  echo
  echo "Summary: $pass_count passed, $fail_count failed"
  if [[ $fail_count -ne 0 ]]; then
    exit 1
  fi
}

main "$@"
