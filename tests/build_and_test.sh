#!/usr/bin/env bash
#
# build_and_test.sh - Convenience script used by the continuous integration
# job and by contributors who want a single command that builds the shell
# and runs every test.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$ROOT_DIR" || exit 1

echo "== building =="
make clean
make "$@" || exit 1

echo
echo "== running the test suite =="
make test
exit $?
