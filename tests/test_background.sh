#!/usr/bin/env bash
#
# test_background.sh - Tests for the execution of background jobs, the
# notification of their completion and the reaping of the children.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: background jobs"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cd "$tmpdir" || exit 1

now_ms() {
    date +%s%3N
}

# ---------------------------------------------------------------------
# The prompt must return immediately
# ---------------------------------------------------------------------
start=$(now_ms)
printf 'sleep 3 &\n' | timeout "$TEST_TIMEOUT" "$MINISHELL" >/dev/null 2>&1
end=$(now_ms)
elapsed=$((end - start))
assert_eq "a background job returns control immediately" \
          "$([ "$elapsed" -lt 2000 ] && echo yes || echo "no (${elapsed}ms)")" "yes"

start=$(now_ms)
printf 'sleep 3\n' | timeout "$TEST_TIMEOUT" "$MINISHELL" >/dev/null 2>&1
end=$(now_ms)
elapsed=$((end - start))
assert_eq "a foreground job blocks the shell" \
          "$([ "$elapsed" -ge 2500 ] && echo yes || echo "no (${elapsed}ms)")" "yes"

# ---------------------------------------------------------------------
# Job notifications in a long running shell
# ---------------------------------------------------------------------
fifo="$tmpdir/ctl"
mkfifo "$fifo"
"$MINISHELL" < "$fifo" > "$tmpdir/bg.out" 2>&1 &
shell_pid=$!
exec 9> "$fifo"

printf 'sleep 0.5 &\n' >&9
sleep 2
printf 'exit\n' >&9
exec 9>&-
wait "$shell_pid" 2>/dev/null

out=$(cat "$tmpdir/bg.out")
assert_contains "the shell reports the start of a background job" "$out" "[1]"
assert_contains "the shell reports the completion of a background job" "$out" \
                "Done"
assert_contains "the completion message contains the command" "$out" "sleep 0.5"

# ---------------------------------------------------------------------
# The background child must be reaped (no zombie may remain)
# ---------------------------------------------------------------------
fifo2="$tmpdir/ctl2"
mkfifo "$fifo2"
"$MINISHELL" < "$fifo2" > "$tmpdir/bg2.out" 2>&1 &
shell_pid=$!
exec 9> "$fifo2"

printf 'sleep 0.3 &\n' >&9
sleep 1.5

if have ps; then
    states=$(ps -o stat= --ppid "$shell_pid" 2>/dev/null | tr -d ' \n')
    assert_not_contains "the finished background child has been reaped" \
                        "$states" "Z"
else
    skip_test "the finished background child has been reaped" "ps is missing"
fi

printf 'exit\n' >&9
exec 9>&-
wait "$shell_pid" 2>/dev/null

# ---------------------------------------------------------------------
# Background jobs with redirections and pipelines
# ---------------------------------------------------------------------
out=$(run_shell "sleep 0.2 & 
")
assert_contains "a background job with a trailing space is accepted" "$out" ""

fifo3="$tmpdir/ctl3"
mkfifo "$fifo3"
"$MINISHELL" < "$fifo3" > "$tmpdir/bg3.out" 2>&1 &
shell_pid=$!
exec 9> "$fifo3"

printf 'echo background-output > bgfile.txt &\n' >&9
sleep 1.5
printf 'exit\n' >&9
exec 9>&-
wait "$shell_pid" 2>/dev/null

assert_file_exists "a background job can redirect its output" \
          "$tmpdir/bgfile.txt"
assert_file_content "the redirection of a background job works" \
          "$tmpdir/bgfile.txt" "background-output"

fifo4="$tmpdir/ctl4"
mkfifo "$fifo4"
"$MINISHELL" < "$fifo4" > "$tmpdir/bg4.out" 2>&1 &
shell_pid=$!
exec 9> "$fifo4"

printf 'echo one | grep one &\n' >&9
sleep 1.5
printf 'exit\n' >&9
exec 9>&-
wait "$shell_pid" 2>/dev/null

out=$(cat "$tmpdir/bg4.out")
assert_contains "a background pipeline is reported as a job" "$out" "[1]"
assert_contains "a background pipeline is reported as finished" "$out" "Done"

# ---------------------------------------------------------------------
# The exit status of the shell is not affected by background jobs
# ---------------------------------------------------------------------
assert_eq "a background job does not change the exit status of the shell" \
          "$(run_shell_status "sleep 0.1 &
exit 3
")" "3"

finish
