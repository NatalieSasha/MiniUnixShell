#!/usr/bin/env bash
#
# experiments/run_all.sh - Run every experiment and store the output.
#
# The output of every experiment is written to results/<name>.txt and the
# combined transcript is written to results/experiments.txt.
#
# Usage:
#   experiments/run_all.sh [output-directory]
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${1:-$ROOT_DIR/results}"

mkdir -p "$OUT_DIR"

EXPERIMENTS=(
    process_creation.sh
    foreground_background.sh
    syscall_trace.sh
    pipeline_experiment.sh
    error_handling.sh
    resource_management.sh
)

COMBINED="$OUT_DIR/experiments.txt"
: > "$COMBINED"

{
    echo "minishell experiment log"
    echo "generated : $(date -u '+%Y-%m-%dT%H:%M:%SZ') (UTC)"
    echo "kernel    : $(uname -a)"
    echo "compiler  : $(gcc --version 2>/dev/null | head -1 || echo 'n/a')"
    echo "shell     : $("$ROOT_DIR/minishell" --version 2>/dev/null || echo 'not built')"
    echo
} >> "$COMBINED"

for exp in "${EXPERIMENTS[@]}"; do
    echo "### running $exp"
    output=$(bash "$SCRIPT_DIR/$exp" 2>&1)
    printf '%s\n' "$output" | tee "$OUT_DIR/${exp%.sh}.txt" > /dev/null
    printf '%s\n' "$output" >> "$COMBINED"
    echo "    -> $OUT_DIR/${exp%.sh}.txt"
done

echo
echo "All experiment transcripts are stored in $OUT_DIR"
