#!/usr/bin/env bash
#
# experiments/resource_management.sh - Experiment G: reaping of children
# and hygiene of the file descriptors.
#
# Objective
#   Verify that the shell does not leave zombie processes behind and that
#   it does not leak file descriptors.
#
# Method
#   1. Start the shell, launch several background jobs and inspect the
#      process table with ps(1) to look for processes in the zombie state.
#   2. Count the descriptors of /proc/<pid>/fd before and after a series
#      of commands with redirections.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiment G: resource management"
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

FIFO="$WORK/ctl"
mkfifo "$FIFO"

"$MINISHELL" < "$FIFO" > "$WORK/out.txt" 2>&1 &
SHELL_PID=$!
exec 9> "$FIFO"

echo "--- 1. Zombies after ten background jobs ----------------------------"
for _ in $(seq 1 10); do
    printf '/bin/true &\n' >&9
done
sleep 2

if command -v ps >/dev/null 2>&1; then
    echo "  process table of the children of the shell (pid $SHELL_PID):"
    ps -o pid=,ppid=,stat=,comm= --ppid "$SHELL_PID" 2>/dev/null | sed 's/^/    /'
    states=$(ps -o stat= --ppid "$SHELL_PID" 2>/dev/null | tr -d ' \n')
    if [ -z "$states" ]; then
        echo "  result: no child process is left -> every child was reaped."
    else
        case "$states" in
            *Z*) echo "  result: a ZOMBIE process was found." ;;
            *)  echo "  result: no zombie, but the following children are"
                 echo "          still running: $states" ;;
        esac
    fi
else
    echo "  SKIPPED: ps(1) is not installed."
fi
echo

echo "--- 2. File descriptor hygiene -------------------------------------"
if [ -d "/proc/$SHELL_PID/fd" ]; then
    fd_before=$(ls "/proc/$SHELL_PID/fd" | wc -l)
    echo "  open descriptors before the test: $fd_before"
    for _ in $(seq 1 30); do
        printf 'echo x > out.txt\ncat < out.txt\n' >&9
    done
    sleep 2
    fd_after=$(ls "/proc/$SHELL_PID/fd" | wc -l)
    echo "  open descriptors after 30 redirections: $fd_after"
    if [ "$fd_before" -eq "$fd_after" ]; then
        echo "  result: no descriptor was leaked."
    else
        echo "  result: $((fd_after - fd_before)) descriptor(s) were leaked."
    fi
    echo "  the descriptors that are currently open:"
    ls -l "/proc/$SHELL_PID/fd" 2>/dev/null | sed 's/^/    /'
else
    echo "  SKIPPED: /proc is not available."
fi

printf 'exit\n' >&9
exec 9>&-
wait "$SHELL_PID" 2>/dev/null
echo
echo "Interpretation:"
echo "  * A child that terminates stays in the process table as a zombie"
echo "    until its parent collects its exit status with waitpid(2).  The"
echo "    shell collects it in process_reap_children(), which calls"
echo "    waitpid(-1, WNOHANG) in a loop."
echo "  * The reaping is deliberately *not* done inside the SIGCHLD handler:"
echo "    a signal handler may only use async-signal-safe functions, and"
echo "    printf(3) is not one of them.  The handler only sets a flag and"
echo "    the actual work happens in the normal control flow of the"
echo "    interpreter loop."
echo "  * Every descriptor that the shell opens for a redirection is closed"
echo "    again, and the pipe descriptors of a pipeline are closed in both"
echo "    the parent and the child."
