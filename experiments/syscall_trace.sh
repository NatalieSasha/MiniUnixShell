#!/usr/bin/env bash
#
# experiments/syscall_trace.sh - Experiment D: redirection and the
# manipulation of file descriptors.
#
# Objective
#   Show the open(2), dup2(2) and close(2) calls that implement the
#   redirection operators and explain the role of each of them.
#
# Method
#   Run the shell under strace(1) with a scripted command that redirects
#   standard input and standard output, and extract the relevant calls.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiment D: redirection and file descriptors"
echo "=============================================================="
echo "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ') (UTC)"
echo "kernel    : $(uname -sr)"
echo

if [ ! -x "$MINISHELL" ]; then
    echo "SKIPPED: $MINISHELL has not been built. Run 'make' first."
    exit 0
fi

if ! command -v strace >/dev/null 2>&1; then
    echo "SKIPPED: strace(1) is not installed on this system."
    exit 0
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

printf 'alpha\nbeta\n' > "$WORK/input.txt"

echo "--- 1. The traced command ------------------------------------------"
echo "The shell is given: 'cat < input.txt > output.txt'"
echo
strace -f -e trace=openat,open,dup,dup2,dup3,close -o "$WORK/trace.txt" \
    "$MINISHELL" < /dev/null > /dev/null 2>&1 <<EOF
cat < $WORK/input.txt > $WORK/output.txt
EOF
echo "--- 2. The relevant calls -----------------------------------------"
echo
grep -E 'openat|dup2|dup3|close\(' "$WORK/trace.txt" \
    | grep -vE 'ENOENT|/etc/ld|/lib/|/usr/lib|/proc/self/maps' \
    | sed -E 's/^[0-9]+ +//' | head -30
echo
echo "--- 3. The resulting file -----------------------------------------"
echo "content of output.txt:"
sed 's/^/    /' "$WORK/output.txt"
echo
echo "--- 4. Interpretation ---------------------------------------------"
echo "  * The shell calls open(2) (glibc: openat(2) with AT_FDCWD) for the"
echo "    redirection target.  The mode argument is 0666; the kernel masks"
echo "    it with the current umask."
echo "  * dup2(2) installs the new descriptor on position 0 or 1.  dup2(2)"
echo "    closes the target descriptor atomically, so there is no window in"
echo "    which the descriptor is invalid."
echo "  * close(2) releases the temporary descriptor that open(2) returned."
echo "  * The library function open(2) and the system call openat(2) are"
echo "    not the same thing: glibc implements open(2) as a wrapper around"
echo "    the openat(2) system call, and that is why openat(2) appears in"
echo "    the trace."
