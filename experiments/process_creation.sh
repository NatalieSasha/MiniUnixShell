#!/usr/bin/env bash
#
# experiments/process_creation.sh - Experiment A: process creation.
#
# Objective
#   Show which process related system calls the shell issues when it runs
#   an external command, and in which order they occur.
#
# Method
#   Run the shell under strace(1) with a scripted (non-interactive) input
#   so that the trace is reproducible.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiment A: process creation"
echo "=============================================================="
echo "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ') (UTC)"
echo "kernel    : $(uname -sr)"
echo "uname -a  : $(uname -a)"
echo "compiler  : $(gcc --version 2>/dev/null | head -1 || echo 'not available')"
echo "shell     : $MINISHELL ($("$MINISHELL" --version 2>/dev/null))"
echo

if ! command -v strace >/dev/null 2>&1; then
    echo "SKIPPED: strace(1) is not installed on this system."
    echo "Install it with: sudo apt install strace"
    exit 0
fi

if [ ! -x "$MINISHELL" ]; then
    echo "SKIPPED: $MINISHELL has not been built. Run 'make' first."
    exit 0
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/input.txt" <<'EOF'
/bin/echo hello
EOF

echo "--- 1. The traced command ------------------------------------------"
echo "The shell is given the single command '/bin/echo hello' on stdin."
echo
echo "\$ strace -f -e trace=process -o trace.txt $MINISHELL < input"
strace -f -e trace=process -o "$WORK/trace.txt" "$MINISHELL" \
    < "$WORK/input.txt" > /dev/null 2>&1
echo
echo "--- 2. The extracted trace -----------------------------------------"
echo
grep -E 'execve|clone|fork|vfork|wait4|exit_group' "$WORK/trace.txt" \
    | sed -E 's/^[0-9]+ +//' | head -40
echo
echo "--- 3. The full trace ----------------------------------------------"
echo
cat "$WORK/trace.txt"
echo
echo "--- 4. Interpretation ---------------------------------------------"
echo "The trace shows the following sequence:"
echo "  * the shell process (pid of the traced program) starts and reads"
echo "    the command line;"
echo "  * a clone(2) (the Linux implementation of fork(2)) creates a child;"
echo "  * the child calls execve(2) to replace its program image with"
echo "    /bin/echo;"
echo "  * the child terminates with exit_group(2);"
echo "  * the parent calls wait4(2) (the Linux implementation of"
echo "    waitpid(2)) and collects the exit status."
echo
echo "Note: the exact sequence of library calls is implementation"
echo "dependent.  glibc implements fork(2) with clone(2) and waitpid(2)"
echo "with wait4(2); the POSIX names fork(2) and waitpid(2) are the"
echo "portable interface that the source code in src/process.c and"
echo "src/pipeline.c uses."
