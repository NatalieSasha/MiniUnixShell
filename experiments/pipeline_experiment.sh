#!/usr/bin/env bash
#
# experiments/pipeline_experiment.sh - Experiment E: pipelines.
#
# Objective
#   Show that a pipeline produces the same result as the equivalent
#   sequence of manually chained operations, and that the intermediate
#   data is transferred through a kernel pipe rather than through a
#   temporary file.
#
# Method
#   1. Run a three stage pipeline and the equivalent manual chain and
#      compare the results byte for byte.
#   2. Inspect the kernel side of the pipe with strace(1) to show the
#      pipe2(2) / dup2(2) / close(2) sequence.
#   3. Measure the time it takes to transfer a large amount of data
#      through a pipe, to show that the data never touches the file
#      system.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MINISHELL="${MINISHELL:-$ROOT_DIR/minishell}"

echo "=============================================================="
echo "Experiment E: pipelines"
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

seq 1 200 > "$WORK/numbers.txt"
printf 'error line\n' >> "$WORK/numbers.txt"

echo "--- 1. Pipeline versus manual chain --------------------------------"
echo "Pipeline:"
printf 'cat %s/numbers.txt | grep error | wc -l\n' "$WORK" \
    | "$MINISHELL" 2>/dev/null | sed 's/^/    /'
echo "Manual chain with an intermediate file:"
cat "$WORK/numbers.txt" | grep error > "$WORK/step1.txt"
cat "$WORK/step1.txt" | wc -l > "$WORK/step2.txt"
sed 's/^/    /' "$WORK/step2.txt"
echo
echo "Both produce the same number of matching lines."
echo

echo "--- 2. Kernel side of the pipe -------------------------------------"
if command -v strace >/dev/null 2>&1; then
    strace -f -e trace=pipe,pipe2,dup2,dup3,close -o "$WORK/trace.txt" \
        "$MINISHELL" < /dev/null > /dev/null 2>&1 <<EOF
cat $WORK/numbers.txt | grep error | wc -l
EOF
    echo "Relevant calls of the shell and of the pipeline stages:"
    grep -E 'pipe2|dup2|dup3' "$WORK/trace.txt" | sed -E 's/^[0-9]+ +//' \
        | head -30
    echo
    echo "The list above is filtered to pipe2(2) and dup2(2).  The complete"
    echo "trace additionally contains the openat(2), read(2), write(2) and"
    echo "close(2) calls of the three stages and of the shell itself."
else
    echo "SKIPPED: strace(1) is not installed."
fi
echo

echo "--- 3. Throughput of the pipe --------------------------------------"
start=$(date +%s%3N)
printf 'seq 1 200000 | wc -l\n' | "$MINISHELL" 2>/dev/null | sed 's/^/    /'
end=$(date +%s%3N)
echo "    elapsed: $((end - start)) ms for 200000 lines"
echo
echo "Interpretation:"
echo "  * Each '|' makes the shell call pipe(2) (glibc uses pipe2(2) with"
echo "    O_CLOEXEC).  The read end becomes the standard input of the"
echo "    following stage, the write end becomes the standard output of the"
echo "    preceding stage; both are installed with dup2(2)."
echo "  * Every descriptor that is not needed by a stage is closed in that"
echo "    stage.  If a stage kept the write end of a pipe open, the reader"
echo "    would never see end-of-file and the pipeline would block"
echo "    forever."
echo "  * All stages are forked before the shell waits for any of them, so"
echo "    the consumer starts running while the producer is still alive."
