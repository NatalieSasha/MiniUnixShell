#!/usr/bin/env bash
#
# experiments/foreground_background.sh - Experiments B and C: foreground
# versus background execution.
#
# Objective
#   Measure how long the shell blocks for a foreground command and how
#   long it blocks for a background command, and explain the difference.
#
# Method
#   Feed a single command to the shell and measure the wall clock time of
#   the whole shell invocation with date(1) before and after.  The same
#   measurement is repeated three times to show the stability of the
#   result.  The measurement is coarse (whole seconds are sufficient),
#   which keeps it independent of the load of the machine.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiments B and C: foreground and background execution"
echo "=============================================================="
echo "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ') (UTC)"
echo "kernel    : $(uname -sr)"
echo "compiler  : $(gcc --version 2>/dev/null | head -1 || echo 'not available')"
echo

if [ ! -x "$MINISHELL" ]; then
    echo "SKIPPED: $MINISHELL has not been built. Run 'make' first."
    exit 0
fi

SLEEP_TIME=3
REPEATS=3

measure() {
    # measure <command-line>
    local start end
    start=$(date +%s)
    printf '%s\n' "$1" | "$MINISHELL" >/dev/null 2>&1
    end=$(date +%s)
    printf '%d' "$((end - start))"
}

echo "--- Experiment B: foreground 'sleep $SLEEP_TIME' --------------------"
for i in $(seq 1 "$REPEATS"); do
    printf 'run %d of %d: %d second(s) elapsed\n' "$i" "$REPEATS" \
        "$(measure "sleep $SLEEP_TIME")"
done
echo
echo "--- Experiment C: background 'sleep $SLEEP_TIME &' -----------------"
for i in $(seq 1 "$REPEATS"); do
    printf 'run %d of %d: %d second(s) elapsed\n' "$i" "$REPEATS" \
        "$(measure "sleep $SLEEP_TIME &")"
done
echo

echo "--- Process table during a background job ---------------------------"
echo "The shell prints the job number and the process id of the job:"
printf 'sleep 2 &\n' | "$MINISHELL" 2>&1 >/dev/null | sed 's/^/    /'
echo
echo "Interpretation:"
echo "  * The foreground run blocks for the whole duration of 'sleep'."
echo "    The shell called fork(2), then waitpid(2); waitpid(2) does not"
echo "    return until the child has terminated, so the shell cannot read"
echo "    the next command line before that."
echo "  * The background run returns immediately.  The shell called"
echo "    fork(2) but did not call waitpid(2) for that child; it recorded"
echo "    the process id in its job table and returned to the prompt."
echo "  * The child is not orphaned: it is still a child of the shell."
echo "    It becomes a zombie when it terminates and is reaped by the"
echo "    waitpid(-1, WNOHANG) call in process_reap_children()."
