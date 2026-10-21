#!/usr/bin/env bash
#
# experiments/error_handling.sh - Experiment F: the four different kinds
# of failure.
#
# Objective
#   Distinguish the four failure modes of the shell and show that each of
#   them produces a different diagnostic and a different exit status.
#
#   1. a parse error      - the command line could not be understood
#   2. a failed system call - open(2) failed, for example
#   3. a failed program launch - execvp(3) failed
#   4. a non-zero exit status of a program that ran successfully
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiment F: error handling"
echo "=============================================================="
echo "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ') (UTC)"
echo "kernel    : $(uname -sr)"
echo

if [ ! -x "$MINISHELL" ]; then
    echo "SKIPPED: $MINISHELL has not been built. Run 'make' first."
    exit 0
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

run_case() {
    # run_case <title> <command>
    local title="$1" cmd="$2" out status
    out=$(printf '%s\n' "$cmd" | "$MINISHELL" 2>&1)
    status=$?
    printf '  %s\n' "$title"
    printf '    command : %s\n' "$cmd"
    printf '    output  : %s\n' "${out:-<none>}"
    printf '    status  : %d\n' "$status"
    echo
}

echo "--- 1. Parse error -------------------------------------------------"
run_case "unterminated quote" 'echo "unterminated'
run_case "unsupported syntax" 'echo $HOME'
run_case "redirection without a target" 'echo >'

echo "--- 2. Failed system call ------------------------------------------"
run_case "input file does not exist" "cat < $WORK/does_not_exist"
run_case "output directory does not exist" "echo x > $WORK/no_such_dir/x"

echo "--- 3. Failed program launch ---------------------------------------"
run_case "command not found" 'no_such_command_42'
printf '#!/bin/sh\nexit 0\n' > "$WORK/not_executable"
chmod 644 "$WORK/not_executable"
run_case "command is not executable" "$WORK/not_executable"

echo "--- 4. Non-zero exit status of a program that ran -------------------"
run_case "program exits with 3" "/bin/sh -c 'exit 3'"
run_case "program was killed by SIGKILL" "/bin/sh -c 'kill -9 \$\$'"

echo "--- 5. The shell survives every one of them -------------------------"
out=$(printf 'echo "unterminated\nno_such_command_42\ncat < /nope\necho still-here\n' \
    | "$MINISHELL" 2>&1)
echo "  output:"
printf '%s\n' "$out" | sed 's/^/    /'
case "$out" in
    *still-here*) echo "  result: the shell is still alive and executed the"
                   echo "          last command successfully." ;;
    *)           echo "  result: UNEXPECTED - the shell did not survive." ;;
esac
