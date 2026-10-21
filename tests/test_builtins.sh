#!/usr/bin/env bash
#
# test_builtins.sh - Tests for the shell's built-in commands.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$SCRIPT_DIR/testlib.sh"

echo "integration tests: built-in commands"

# ---------------------------------------------------------------------
# help
# ---------------------------------------------------------------------
out=$(run_shell "help
")
assert_contains "help prints a help text" "$out" "Built-in commands"
assert_contains "help lists the 'cd' built-in" "$out" "cd [directory]"
assert_contains "help lists the 'exit' built-in" "$out" "exit [status]"
assert_eq "help exits with status 0" "$(run_shell_status "help
")" "0"

out=$(run_shell "help extra
")
assert_contains "help rejects extra arguments" "$out" "too many arguments"
assert_eq "help with extra arguments exits with status 2" \
          "$(run_shell_status "help extra
")" "2"

# ---------------------------------------------------------------------
# pwd
# ---------------------------------------------------------------------
expected_pwd=$(pwd)
out=$(run_shell "pwd
")
assert_eq "pwd prints the current working directory" "$out" "$expected_pwd"
assert_eq "pwd exits with status 0" "$(run_shell_status "pwd
")" "0"

out=$(run_shell "pwd extra
")
assert_contains "pwd rejects extra arguments" "$out" "too many arguments"

# ---------------------------------------------------------------------
# cd
# ---------------------------------------------------------------------
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

out=$(run_shell "cd $tmpdir
pwd
")
assert_eq "cd changes the working directory of the shell" "$out" "$tmpdir"

out=$(run_shell "cd $tmpdir
cd /
pwd
")
assert_contains "cd accepts an absolute path" "$out" "/"

out=$(run_shell "cd $tmpdir
cd ..
pwd
")
parent=$(dirname "$tmpdir")
assert_eq "cd accepts a relative path" "$out" "$parent"

out=$(run_shell "cd /this/directory/does/not/exist
")
assert_contains "cd reports an error for a missing directory" "$out" \
                "No such file or directory"
assert_eq "cd with a missing directory exits with status 1" \
          "$(run_shell_status "cd /this/directory/does/not/exist
")" "1"

out=$(run_shell "cd a b
")
assert_contains "cd rejects more than one argument" "$out" "too many arguments"
assert_eq "cd with two arguments exits with status 2" \
          "$(run_shell_status "cd a b
")" "2"

# cd without an argument changes to $HOME
out=$(HOME="$tmpdir" run_shell "cd
pwd
")
assert_eq "cd without an argument changes to \$HOME" "$out" "$tmpdir"

out=$(HOME= run_shell "cd
")
assert_contains "cd without an argument reports an unset \$HOME" "$out" \
                "HOME is not set"

# The directory change must survive between two commands of the same
# shell instance (this is what distinguishes a built-in from a subshell).
out=$(run_shell "cd $tmpdir
pwd
")
assert_eq "the cd state persists for the next command" "$out" "$tmpdir"

# ---------------------------------------------------------------------
# exit
# ---------------------------------------------------------------------
assert_eq "exit without an argument terminates with status 0" \
          "$(run_shell_status "exit
")" "0"
assert_eq "exit 3 terminates with status 3" "$(run_shell_status "exit 3
")" "3"
assert_eq "exit 255 terminates with status 255" \
          "$(run_shell_status "exit 255
")" "255"

out=$(run_shell "exit notanumber
")
assert_contains "exit rejects a non-numeric argument" "$out" \
                "numeric argument required"

out=$(run_shell "exit 300
")
assert_contains "exit rejects a status outside 0-255" "$out" \
                "range 0-255"

out=$(run_shell "exit 1 2
")
assert_contains "exit rejects more than one argument" "$out" \
                "too many arguments"

# `exit` inside a pipeline only terminates the pipeline stage
out=$(run_shell "echo before | exit 5
echo after
")
assert_contains "the shell continues after 'exit' inside a pipeline" "$out" \
                "after"

# ---------------------------------------------------------------------
# The shell must survive recoverable errors
# ---------------------------------------------------------------------
out=$(run_shell "cd /no/such/directory
pwd
")
assert_contains "the shell keeps running after a failed built-in" "$out" \
                "$expected_pwd"

finish
