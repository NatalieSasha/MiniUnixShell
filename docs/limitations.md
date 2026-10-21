# Limitations and known issues

This document is deliberately explicit about what the project does **not**
do.  Every statement here was checked against the implementation in
`src/`; the "status" column refers to the verification described in
[experimental_methodology.md](experimental_methodology.md).

---

## 1. Summary

| # | Limitation                                              | Status                                  |
|---|---------------------------------------------------------|-----------------------------------------|
| 1 | No full job control (no process groups, no `fg`/`bg`)    | not implemented, documented              |
| 2 | No variable expansion, command substitution or globbing   | not implemented, explicitly rejected    |
| 3 | No command separator `;`                                 | not implemented, explicitly rejected    |
| 4 | No here-documents                                        | not implemented, explicitly rejected    |
| 5 | No `~` expansion                                         | not implemented                         |
| 6 | No built-in `cd -` (previous directory)                   | not implemented                         |
| 7 | `pwd` always prints the physical path                    | by design                               |
| 8 | Only the last stage of a pipeline determines the status  | by design, POSIX convention             |
| 9 | A background job is reported only when the shell is idle  | by design                               |
| 10| The intermediate stages of a background pipeline are not reported | by design                         |
| 11| The prompt is not configurable                            | not implemented                         |
| 12| The shell is not a POSIX shell                            | by design                               |

---

## 2. Job control

**What is missing.**  The shell does not create a new process group for a
foreground job, does not use `tcsetpgrp(3)` to hand the terminal over to
that group, and does not implement `fg`, `bg`, `jobs` or `kill %1`.

**Consequences.**

* Ctrl+C is delivered by the terminal to the whole foreground process
  group.  Because the shell and its children share one process group, the
  `SIGINT` reaches the shell as well.  The shell blocks `SIGINT` while a
  foreground child runs, so the shell itself survives - but a *background*
  child that is still in the same process group would also receive the
  signal.  The shell therefore sets `SIGINT` to `SIG_IGN` in background
  children explicitly, which neutralises the problem for them.
* The user cannot suspend a command with Ctrl+Z and resume it later.
* The shell cannot bring a background job into the foreground, so a
  program that expects to read from the terminal cannot be used in the
  background.  This is why the standard input of a background job is
  redirected to `/dev/null` (POSIX behaviour for asynchronous lists).

**What would be needed.**  `setpgid(2)` (or `tcsetpgrp(3)`) in the child,
`tcsetpgrp(3)` in the parent to give the terminal to the foreground
group and to take it back afterwards, a handler for `SIGTSTP` and
`SIGCONT`, and a job table that stores the process group id.  This is a
substantial extension and was deliberately left out of the scope: it would
have made the rest of the implementation harder to explain and to verify.

**Verification status:** the *absence* of job control is verified
(`test_signals.sh` shows that the shell survives `SIGINT` and that a
foreground child is interrupted correctly).  The *presence* of job
control is not claimed and is not tested, because it does not exist.

---

## 3. Syntax that is deliberately not supported

The parser rejects the following constructs with the diagnostic
`unsupported shell syntax` instead of ignoring them:

```
echo $HOME            variable expansion
echo `date`           command substitution
echo $(date)          command substitution
ls *.c                pattern matching
cat << EOF            here-documents
echo a; echo b        command separator
(echo a)              subshells and grouping
```

**Rationale.**  A shell that silently ignores an unsupported construct
would execute something different from what the user wrote.  For example,
`echo *.c` in a shell without globbing would print the literal string
`*.c`, which is surprising; `ls *.c` would list a file that is called
`*.c`.  Rejecting the construct is louder and therefore safer.

**Workaround.**  Use single quotes for a literal dollar sign, for example
`echo '$HOME'`.  Quoted patterns are passed through literally, exactly as
in a POSIX shell.

---

## 4. Known deviations from the POSIX shell

| Deviation                                              | Reason                                              |
|--------------------------------------------------------|-----------------------------------------------------|
| A pipeline stage must contain at least one word        | keeps the grammar unambiguous; `cat \| > f` is rejected |
| Redirections may not be repeated for the same file in one command | the last redirection wins, as in POSIX      |
| The prompt is printed to standard error                 | keeps redirected output free of prompt characters    |
| The prompt is only printed when standard input is a terminal | keeps scripted runs clean                       |
| `exit` inside a pipeline only ends that stage           | the stage is a child process, as in POSIX            |
| A built-in inside a pipeline cannot change shell state  | the stage is a child process, as in POSIX            |

---

## 5. Behavioural details that are easy to get wrong

* **`pwd` prints the physical path.**  `getcwd(3)` resolves symbolic
  links; the shell does not implement the logical path (`pwd -L`).
* **The exit status of a pipeline is the status of the last stage.**
  The status of the earlier stages is discarded.  This is the POSIX
  convention, but it is worth stating because the opposite convention
  also exists.
* **`cd` without an argument changes to `$HOME`.**  If `HOME` is unset
  or empty the command fails with `cd: HOME is not set` and status 1.
* **A background job does not change the exit status of the shell.**
  The status of the shell is the status of the `exit` built-in, or the
  status of the last foreground command when the input ends.
* **A file created with `>` has the mode `0666 & ~umask`.**  With the
  usual `umask 022` this is `0644`.  The shell does not change the
  `umask`.
* **A job notification is only printed when the shell is idle.**  If the
  user is in the middle of typing a line when a background job finishes,
  the notification is printed after the line has been submitted.  The
  prompt is not redrawn in the middle of a line that is being typed.

---

## 6. Resource limitations

| Resource                    | Bound                                            |
|-----------------------------|--------------------------------------------------|
| length of a command line    | 65536 bytes (`PARSER_MAX_LINE_LENGTH`)           |
| buffered input per line     | 1 MiB (`LINE_BUFFER_LIMIT`)                      |
| words per command           | 1024 (`PARSER_MAX_WORDS`)                        |
| open file descriptors       | the usual `RLIMIT_NOFILE` of the process         |
| number of pipeline stages   | limited by the number of processes and by `RLIMIT_NPROC` |

A command line that is too long produces `command line is too long`; a
command with too many arguments produces `too many arguments`.  Both
limits exist so that a single line cannot exhaust the memory of the
shell.

---

## 7. Portability

* The code is written against POSIX.1-2008 and compiles cleanly with
  `-std=c11 -Wall -Wextra -Wpedantic` on GCC 13.
* The code has **not** been tested on a non-Linux POSIX system.  The
  behaviour that is Linux specific is limited to the observation of the
  system call names in the traces (`clone(2)`, `wait4(2)`, `openat(2)`);
  the program itself uses only portable interfaces.
* The project has **not** been tested on macOS, on a BSD or on a
  non-Linux Unix.
* The project does **not** run natively on Windows.  Windows provides
  neither `fork(2)` nor `execvp(3)`; a native Windows port would need
  `CreateProcess(2)`, which is a completely different model.  The
  project is therefore developed on Windows but executed on Linux; see
  the README for the instructions.
