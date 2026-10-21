# Experimental methodology

This document describes how the experiments were set up, which tools were
used, what was measured and which conclusions the measurements support.
It also states, for every experiment, whether the result was actually
observed or whether it is still pending.

---

## 1. Verification environment

All measurements that are recorded in `results/` and in
[report.md](report.md) were produced in the environment below.

| Property                | Value                                                     |
|-------------------------|-----------------------------------------------------------|
| Distribution            | Ubuntu 24.04.5 LTS (Noble Numbat)                        |
| Kernel                  | Linux 6.6.87.2-microsoft-standard-WSL2 x86_64             |
| Compiler                | gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0              |
| C library               | glibc 2.39 (Ubuntu GLIBC 2.39-0ubuntu8.9)                 |
| Build system            | GNU Make 4.3                                              |
| Tracing tool            | strace 6.8                                                |
| Process inspection      | procps (`ps`), `/proc`                                    |
| CPU                     | Intel(R) Core(TM) Ultra 5 125H, 18 logical processors     |
| Memory                  | 7.5 GiB                                                   |
| Container runtime       | Docker Desktop 4.55.0, Linux containers on WSL2            |

**Important qualification.**  The verification environment is an Ubuntu
24.04 container running on WSL2, not a bare-metal installation.  The
kernel is the WSL2 kernel, which is not the kernel of a physical Ubuntu
installation.  The *user space* - the distribution, the compiler, the C
library and all the tools - is the genuine Ubuntu 24.04 user space.  The
conclusions of the experiments concern the behaviour of the programs, and
these are determined by the user space libraries and by the documented
kernel interfaces; they should nevertheless be re-verified on a physical
Ubuntu 24.04 machine before the results are quoted as "Ubuntu 24.04
results" in a formal submission.

**Kali Linux was not used for the verification.**  Kali is a Debian
derivative and uses the same glibc and the same POSIX interfaces, so the
project is expected to build and to behave identically there, but this
expectation has **not** been verified.  See the "Verification status"
section of the report.

---

## 2. Test methodology

The project distinguishes four kinds of evidence.

| Kind                | Where                                  | What it proves                                            |
|---------------------|----------------------------------------|-----------------------------------------------------------|
| Unit test           | `tests/test_parser.c`                  | the behaviour of the parser in isolation                  |
| Integration test    | `tests/test_*.sh`                      | the behaviour of the built shell binary                    |
| Manual test         | the example session in the README      | the behaviour of the interactive shell                     |
| Experiment          | `experiments/*.sh`                     | the behaviour of the operating system                     |

### 2.1 Integration tests

Every integration test script

* creates its own temporary directory with `mktemp(1)` and removes it
  again, so no user file is ever overwritten,
* drives the **real** executable (`./minishell`) with a scripted standard
  input, and
* asserts on the output, on the exit status, or on the content of a
  file that the shell produced.

Assertions are deterministic: the expected values are computed from the
environment (for example `$(pwd)`), never from a measurement.  Where a
timing property is the object of the test, a generous threshold is used
(2 seconds for a job that should return immediately, 2.5 seconds for a
job that should block for 3 seconds), so that a slow or loaded machine
cannot turn the test into a false negative.

Every invocation of the shell is wrapped in `timeout(1)`, so a hang
becomes a test failure instead of a blocked test run.

### 2.2 Sanitizers

The whole test suite was executed with AddressSanitizer and
UndefinedBehaviorSanitizer:

```
make sanitize-test
```

which rebuilds the shell with

```
-fsanitize=address,undefined -fno-omit-frame-pointer
```

and runs the suite with `ASAN_OPTIONS=detect_leaks=1` and
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`.

The sanitizers found two real defects during development; both were
fixed and are listed in the report.

### 2.3 The interactive test

Some properties cannot be tested with a scripted input:

* the prompt is printed and behaves correctly on a terminal,
* Ctrl+C interrupts the foreground command and leaves the shell alive,
* the `Done` notification of a background job appears on the terminal.

The first and the third are covered indirectly by the integration tests
(the notification is written to standard error and is therefore visible
in the captured output), but the *interactive* behaviour itself was only
exercised manually.  This is stated explicitly instead of being claimed
as automated coverage.

---

## 3. Experiment A - process creation

**Objective.**  Determine which process related system calls the shell
issues when it executes an external command.

**Method.**

```
strace -f -e trace=process -o trace.txt ./minishell < input.txt
```

`-f` makes `strace` follow the child processes, `-e trace=process`
restricts the trace to the process related calls (`clone`, `execve`,
`wait4`, `exit_group`, ...).  The input is a file, not a terminal, so
the run is scripted and reproducible.

**Result.**  See `results/process_creation.txt`.  The observed sequence
is

```
execve("/work/minishell", ...)                        = 0
clone(...)                                            = 74
execve("/bin/echo", ["echo", "hello"], ...)           = 0
exit_group(0)
wait4(74, ...)                                        = 74
```

The process id that appears in the trace is assigned by the kernel and
therefore differs from run to run; the *sequence* of the calls is what the
experiment establishes.

**Interpretation.**  `fork(2)` is implemented by glibc with `clone(2)`
and `waitpid(2)` with `wait4(2)`; the portable POSIX names are what the
source code uses.  The trace also shows the `PATH` search of
`execvp(3)`: one `execve(2)` per directory of `PATH` until one succeeds.

---

## 4. Experiment B - foreground execution

**Objective.**  Show that the shell waits for a foreground command.

**Method.**  `sleep 3` is given to the shell on standard input and the
wall clock time of the whole shell invocation is measured with
`date(1)`.  The measurement is repeated three times.

**Result.**  See `results/foreground_background.txt`: three seconds in
each of the three runs.

**Interpretation.**  The shell called `fork(2)` and then `waitpid(2)`.
`waitpid(2)` blocks until the child has terminated, so the shell cannot
read the next command line before that.  The exit status that
`waitpid(2)` returns is the one that the shell reports.

---

## 5. Experiment C - background execution

**Objective.**  Show that the shell does not wait for a background
command.

**Method.**  The same measurement with `sleep 3 &`.

**Result.**  See `results/foreground_background.txt`: zero seconds in
each of the three runs.  The shell printed `[1] 133`, i.e. the job number
and the process id of the background job.

**Interpretation.**  The shell called `fork(2)` and did **not** call
`waitpid(2)` for that child.  It recorded the process id in the job
table and returned to the prompt.  The child is not an orphan: it is
still a child of the shell and becomes a zombie when it terminates, until
the shell collects it with `waitpid(-1, WNOHANG)`.

---

## 6. Experiment D - redirection

**Objective.**  Show the descriptor operations that implement `<`, `>`
and `>>`.

**Method.**

```
strace -f -e trace=openat,open,dup,dup2,dup3,close -o trace.txt ./minishell
```

with the command `cat < input.txt > output.txt`.

**Result.**  See `results/syscall_trace.txt`:

```
openat(AT_FDCWD, ".../input.txt",  O_RDONLY)              = 3
dup2(3, 0)                                               = 0
close(3)                                                 = 0
openat(AT_FDCWD, ".../output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
dup2(3, 1)                                               = 1
close(3)                                                 = 0
```

**Interpretation.**  `open(2)` is a library function; glibc implements it
with the `openat(2)` system call, which is why `openat(2)` appears in the
trace.  `dup2(2)` installs the new descriptor on position 0 or 1 and
closes the old one atomically.  `close(2)` releases the temporary
descriptor.  The mode `0666` is masked by the `umask` of the process.

---

## 7. Experiment E - pipelines

**Objective.**  Show that a pipeline is equivalent to the corresponding
manual chain, and observe the descriptor plumbing.

**Method.**

1. Compare `cat numbers.txt | grep error | wc -l` with the same chain
   executed manually with an intermediate file.
2. Trace the pipeline with
   `strace -f -e trace=pipe,pipe2,dup2,dup3,close`.
3. Measure the time needed to push 200000 lines through a pipe.

**Result.**  See `results/pipeline_experiment.txt`.  Both variants
produce the same result.  The trace shows one `pipe2(2)` per `|` and the
corresponding `dup2(2)` calls.  In the recorded run 200000 lines were
transferred in 112 ms; the wall clock time depends on the machine and
varies between runs, so it is reported as an observation of one run and
not as a benchmark.

**Interpretation.**  Each `|` makes the shell call `pipe(2)`; the read
end becomes the standard input of the next stage and the write end the
standard output of the previous one, both installed with `dup2(2)`.  The
descriptors that a stage does not need are closed in that stage, which is
what makes the end-of-file condition observable for the reader.

---

## 8. Experiment F - error handling

**Objective.**  Distinguish the four kinds of failure.

**Method.**  Each failure case is given to the shell and the diagnostic
and the exit status are recorded.

**Result.**  See `results/error_handling.txt`.

| Kind                       | Example              | Diagnostic                              | Status |
|----------------------------|----------------------|-----------------------------------------|--------|
| parse error                | `echo "abc`          | `unterminated quoted string`            | unchanged |
| failed system call         | `cat < missing`      | `minishell: missing: No such file or directory` | 1 |
| failed program launch      | `no_such_command_42` | `minishell: no_such_command_42: No such file or directory` | 127 |
| non-zero exit status       | `/bin/sh -c 'exit 3'`| (none)                                  | 3 |

**Interpretation.**  A parse error is detected by the shell itself, a
failed system call is reported by the function that failed, a failed
program launch is reported by the *child* (which then terminates with
127 or 126), and a non-zero exit status is a completely normal outcome
of a program that ran successfully.  The shell survives all four.

The status column is the exit status of the shell after that single
command.  A parse error leaves it **unchanged**, because no command was
executed and there is therefore no new status to report; the shell keeps
the status of the last command it actually ran.  This was confirmed
directly:

```
printf 'echo "unterminated\n' | ./minishell                 -> status 0
printf 'no_such_cmd_xyz\necho "unterminated\n' | ./minishell -> status 127
printf '/bin/true\necho "unterminated\n' | ./minishell       -> status 0
```

---

## 9. Experiment G - resource management

**Objective.**  Verify that no zombie process and no leaked file
descriptor is left behind.

**Method.**

1. Start the shell with a FIFO as standard input, launch ten background
   jobs, wait for them to finish and inspect the children of the shell
   with `ps(1)`.
2. Count the entries of `/proc/<pid>/fd` before and after thirty
   commands with redirections.

**Result.**  See `results/resource_management.txt`: the process table
contains no child of the shell any more (every child was reaped), and the
number of open descriptors is 3 before and 3 after the test - the three
standard descriptors and nothing else.

**Interpretation.**  The reaping is done by `waitpid(-1, WNOHANG)` in
`process_reap_children()`, which runs in the normal control flow of the
interpreter loop and not in the signal handler.  Every descriptor that
the shell opens for a redirection is closed again, and the pipe
descriptors are closed in both the parent and the child.

---

## 10. Reproducing the results

```sh
cd mini-unix-shell
make
make test
experiments/run_all.sh          # writes results/*.txt
```

The scripts in `experiments/` only use `strace(1)`, `ps(1)`, `date(1)`
and the standard shell utilities.  If `strace(1)` is not installed, the
corresponding experiment prints `SKIPPED` instead of a result.
