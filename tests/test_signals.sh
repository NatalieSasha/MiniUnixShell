#!/usr/bin/env bash
#
# test_signals.sh - Tests for the signal handling of the shell.
#
# The tests use a FIFO so that the shell can be driven step by step and so
# that the shell process can be inspected with ps(1) while it is running.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: signals"

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

cd "$tmpdir" || exit 1

# Start a shell that reads its commands from a FIFO.
start_shell() {
    local fifo="$1" outfile="$2"
    mkfifo "$fifo"
    "$MINISHELL" < "$fifo" > "$outfile" 2>&1 &
    SHELL_PID=$!
    exec 9> "$fifo"
}

stop_shell() {
    printf 'exit\n' >&9
    exec 9>&-
    wait "$SHELL_PID" 2>/dev/null
}

# ---------------------------------------------------------------------
# SIGINT must not kill the shell
# ---------------------------------------------------------------------
start_shell "$tmpdir/f1" "$tmpdir/o1"
printf 'echo alive\n' >&9
sleep 0.5
kill -INT "$SHELL_PID" 2>/dev/null
sleep 0.5
if kill -0 "$SHELL_PID" 2>/dev/null; then
    assert_eq "SIGINT does not terminate the shell" "alive" "alive"
else
    assert_eq "SIGINT does not terminate the shell" "dead" "alive"
fi
printf 'echo still-alive\n' >&9
sleep 0.5
stop_shell
out=$(cat "$tmpdir/o1")
assert_contains "the shell still executes commands after SIGINT" "$out" \
                "still-alive"

# ---------------------------------------------------------------------
# A foreground command must be interruptible, the shell must survive
# ---------------------------------------------------------------------
start_shell "$tmpdir/f2" "$tmpdir/o2"
printf 'sleep 30\n' >&9
sleep 0.7

child=""
if have ps; then
    child=$(ps -o pid= --ppid "$SHELL_PID" 2>/dev/null | tr -d ' ' | head -1)
fi

if [ -n "$child" ]; then
    kill -INT "$child" 2>/dev/null
    sleep 0.7
    if kill -0 "$SHELL_PID" 2>/dev/null; then
        assert_eq "the shell survives the interruption of a foreground command" \
                  "alive" "alive"
    else
        assert_eq "the shell survives the interruption of a foreground command" \
                  "dead" "alive"
    fi
    # Closing the FIFO produces end of file; the shell then terminates with
    # the status of the last command, which is 128 + SIGINT = 130.
    exec 9>&-
    wait "$SHELL_PID" 2>/dev/null
    status=$?
    assert_eq "an interrupted foreground command reports status 130" \
              "$status" "130"
else
    skip_test "the shell survives the interruption of a foreground command" \
              "no child process found"
    stop_shell
fi

# ---------------------------------------------------------------------
# SIGCHLD must not disturb the shell
# ---------------------------------------------------------------------
start_shell "$tmpdir/f3" "$tmpdir/o3"
for _ in 1 2 3 4 5; do
    printf '/bin/true\n' >&9
done
sleep 0.7
printf 'echo after-children\n' >&9
sleep 0.5
stop_shell
out=$(cat "$tmpdir/o3")
assert_contains "the shell keeps working while children terminate" "$out" \
                "after-children"

# ---------------------------------------------------------------------
# Many background jobs must not leave zombies behind
# ---------------------------------------------------------------------
start_shell "$tmpdir/f4" "$tmpdir/o4"
for _ in 1 2 3 4 5 6 7 8; do
    printf '/bin/true &\n' >&9
done
sleep 1.5
if have ps; then
    states=$(ps -o stat= --ppid "$SHELL_PID" 2>/dev/null | tr -d ' \n')
    assert_not_contains "no zombie is left after eight background jobs" \
                        "$states" "Z"
else
    skip_test "no zombie is left after eight background jobs" "ps is missing"
fi
stop_shell

# ---------------------------------------------------------------------
# A command that is killed by a signal reports 128 + signal number
# ---------------------------------------------------------------------
assert_eq "a command killed by SIGKILL reports status 137" \
          "$(run_shell_status "/bin/sh -c 'kill -9 \$\$'
")" "137"

finish
