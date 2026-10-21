# Mini Unix Shell

`minishell` is a Unix command interpreter written from scratch in C11.  It
reads a command line, parses it according to an explicitly documented
grammar, and executes it using the POSIX process interfaces `fork(2)`,
`execvp(3)`, `waitpid(2)`, `pipe(2)`, `dup2(2)`, `open(2)`, `close(2)` and
`sigaction(2)`.

The project is a case study in the Unix process model.  Its purpose is to
make the mechanism behind a shell observable and verifiable: every claim
about process creation, redirection, pipelines, background execution and
signal handling is backed either by an automated test or by a stored system
call trace.

The project is **developed on Windows** and **executed on Linux**.  It uses
POSIX interfaces only and does not run natively on Windows; see
[Platform support](#platform-support) and
[docs/report.md](docs/report.md) section 26.

---

## Table of contents

1. [Features](#features)
2. [Example session](#example-session)
3. [Requirements](#requirements)
4. [Installation on Ubuntu](#installation-on-ubuntu)
5. [Installation on Kali Linux](#installation-on-kali-linux)
6. [Building](#building)
7. [Running the shell](#running-the-shell)
8. [Supported command syntax](#supported-command-syntax)
9. [Built-in commands](#built-in-commands)
10. [Exit status conventions](#exit-status-conventions)
11. [Makefile targets](#makefile-targets)
12. [Running the tests](#running-the-tests)
13. [Running the sanitizers](#running-the-sanitizers)
14. [Running the experiments](#running-the-experiments)
15. [Using the project with VS Code on Windows](#using-the-project-with-vs-code-on-windows)
16. [Project structure](#project-structure)
17. [Documentation](#documentation)
18. [Platform support and verification status](#platform-support-and-verification-status)
19. [Known limitations](#known-limitations)
20. [License](#license)

---

## Features

- Four built-in commands: `help`, `pwd`, `cd`, `exit`.
- Quoting with single quotes, double quotes and backslash escapes.
- Input redirection (`<`), output redirection (`>`) and append (`>>`).
- Pipelines of any number of stages (`|`).
- Background execution (`&`) with a job table and a completion
  notification.
- Exit status conventions of a POSIX shell: 0, 1, 2, 126, 127 and
  `128 + signal`.
- Unsupported syntax is **rejected** with a diagnostic instead of being
  silently ignored.
- An automated test suite with 173 assertions in seven programs, plus a
  variant that runs the whole suite under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Seven experiments that trace the shell with `strace(1)` and store the
  transcripts in `results/`.

---

## Example session

```
minishell$ pwd
/home/student/mini-unix-shell
minishell$ help
minishell 1.0.0 - a small Unix-like shell

Built-in commands:
  help              show this help text
  pwd               print the current working directory
  cd [directory]    change the current working directory
                    (without an argument the shell changes to $HOME)
  exit [status]     leave the shell; status is an integer 0-255

Command syntax:
  cmd args...            run an external program
  cmd | cmd | cmd        connect commands with a pipe
  cmd < file             read standard input from file
  cmd > file             write standard output to file (truncate)
  cmd >> file            append standard output to file
  cmd &                  run the command in the background
  'text' "text" \c       quoting and escaping

Not supported: variable expansion, command substitution, globbing,
here-documents, command separators (';') and job control.
minishell$ echo "hello   world"
hello   world
minishell$ echo 'no $expansion here'
no $expansion here
minishell$ ls -l | grep minishell | wc -l
1
minishell$ cat < /etc/hostname > host.txt
minishell$ cat host.txt
minishell$ sleep 30 &
[1] 4711
minishell$ exit 0
```

---

## Requirements

| Requirement | Version used for the verification |
|---|---|
| A POSIX system | Linux (Ubuntu 24.04 user space) |
| A C11 compiler | GCC 13.3.0 |
| GNU Make | 4.3 |
| POSIX shell (`bash` or `dash`) | for the test and experiment scripts |
| `strace` (optional) | 6.8, only needed for the experiments |
| `procps` (optional) | only needed for Experiment G |

There are no third-party libraries.  The shell links only against the C
library.

---

## Installation on Ubuntu

```sh
sudo apt update
sudo apt install build-essential make strace procps
```

`build-essential` provides GCC and the standard headers; `make` is needed
for the build; `strace` and `procps` are only needed to run the
experiments.

Then get the source and build it:

```sh
cd mini-unix-shell
make
```

If you only want to build the shell and run the tests, `build-essential`
alone is sufficient.

---

## Installation on Kali Linux

Kali is a Debian derivative and uses the same package names:

```sh
sudo apt update
sudo apt install build-essential make strace procps
```

Then, as above:

```sh
cd mini-unix-shell
make
```

**Verification note.**  The project was *not* executed on Kali during
development; all measurements were taken in an Ubuntu 24.04 environment.
Kali is expected to behave identically because it uses the same glibc and
the same POSIX interfaces, but that expectation has not been tested.  See
[docs/report.md](docs/report.md) section 26.3 for the exact status of
every platform.

---

## Building

```sh
make
```

This produces the executable `./minishell`.  The compiler options are

```
-std=c11 -Wall -Wextra -Wpedantic -O2 -g
-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
```

and the build is warning-free with GCC 13.

Every part of the toolchain can be overridden:

```sh
make CC=clang
make CFLAGS="-std=c11 -Wall -Wextra -O3"
```

---

## Running the shell

```sh
make run
```

or, after `make`,

```sh
./minishell
```

The shell reads from standard input, so it can be scripted as well:

```sh
printf 'ls -l | wc -l\nexit 0\n' | ./minishell
```

Two options are recognised:

```
  -h, --help     show this help text and exit
  -v, --version  show the version and exit
```

---

## Supported command syntax

The complete grammar is in [docs/parser_grammar.md](docs/parser_grammar.md).

| Syntax | Example | Meaning |
|---|---|---|
| simple command | `ls -l` | run an external program |
| single quotes | `echo 'a  b'` | every character is literal |
| double quotes | `echo "a  b"` | blanks are literal, `\"` and `\\` are escapes |
| backslash | `echo a\ b` | escapes the next character |
| empty argument | `echo ""` | one argument, the empty string |
| adjacent quoting | `echo a"b c"d` | one word: `ab cd` |
| input redirection | `cat < file` | read standard input from a file |
| output redirection | `ls > file` | write standard output to a file, truncating it |
| append | `ls >> file` | append to a file |
| pipeline | `cat f \| grep x \| wc -l` | connect commands with a pipe |
| background | `sleep 10 &` | do not wait for the command |

Redirection binds more tightly than the pipe operator, so
`cat < in.txt | grep error > out.txt` is
`( cat < in.txt ) | ( grep error > out.txt )`.

The following are **not** supported and are rejected with the diagnostic
`unsupported shell syntax`:

```
echo $HOME            variable expansion
echo `date`           command substitution
echo $(date)          command substitution
ls *.c                pattern matching
cat << EOF            here-documents
echo a; echo b        command separator
(echo a)              subshells and grouping
```

The shell rejects them on purpose: a shell that silently ignored `*.c`
would run `ls` on a file literally called `*.c`, which is a different
program from the one the user asked for.  Use quotes to pass such text
literally, for example `echo '$HOME'`.

---

## Built-in commands

| Command | Description | Arguments |
|---|---|---|
| `help` | prints the built-in reference and the syntax summary | none |
| `pwd` | prints the current working directory | none |
| `cd [dir]` | changes the working directory; without an argument the shell changes to `$HOME` | zero or one |
| `exit [status]` | leaves the shell with the given status (0-255, default 0) | zero or one |

`pwd` prints the *physical* path, because `getcwd(3)` resolves symbolic
links; the logical path (`pwd -L`) is not implemented.

---

## Exit status conventions

| Status | Meaning |
|---|---|
| 0 | success |
| 1 | general failure (for example, a directory that does not exist) |
| 2 | misuse of a built-in (wrong number of arguments) |
| 126 | the command was found but could not be executed |
| 127 | the command was not found |
| 128 + n | the command was terminated by signal n |
| 130 | the command was interrupted by `SIGINT` (128 + 2) |
| 137 | the command was killed by `SIGKILL` (128 + 9) |

The status of a pipeline is the status of its **last** stage, as in a
POSIX shell.  A background job does not change the exit status of the
shell.

---

## Makefile targets

| Target | Effect |
|---|---|
| `make` or `make all` | build `./minishell` (default) |
| `make run` | build and start an interactive shell |
| `make test` | build and run the complete test suite |
| `make debug` | rebuild with `-O0 -g3 -DDEBUG` |
| `make sanitize` | rebuild with AddressSanitizer and UndefinedBehaviorSanitizer |
| `make sanitize-test` | rebuild with the sanitizers and run the whole suite |
| `make clean` | remove the build artefacts |
| `make help` | print the list of targets |

---

## Running the tests

```sh
make test
```

The suite consists of one unit test program for the parser and six
integration test scripts that drive the real executable.  Every script
creates its own temporary directory, removes it afterwards, and wraps every
invocation of the shell in `timeout(1)` so that a hang becomes a failure
instead of a blocked run.

The expected output of a successful run ends with:

```
===============================================================
summary
===============================================================
passed  : 7
failed  : 0
skipped : 0
  ok    : test_parser
  ok    : test_background.sh
  ok    : test_builtins.sh
  ok    : test_execution.sh
  ok    : test_pipelines.sh
  ok    : test_redirection.sh
  ok    : test_signals.sh

RESULT: all test scripts passed
```

One assertion in `test_redirection.sh` is skipped when the tests run as
`root`, because `root` may write to a file whose mode is `0444`.  The skip
is reported explicitly.

---

## Running the sanitizers

```sh
make sanitize-test
```

This rebuilds the shell with

```
-fsanitize=address,undefined -fno-omit-frame-pointer
```

and runs the whole suite with `ASAN_OPTIONS=detect_leaks=1` and
`UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`.  The suite currently
passes without a single sanitizer report.

---

## Running the experiments

```sh
make
experiments/run_all.sh
```

The scripts use `strace(1)`, `ps(1)` and `date(1)`.  Each experiment writes
a transcript to `results/<name>.txt`, and the combined log goes to
`results/experiments.txt`.  If `strace(1)` is not installed, the
corresponding experiment prints `SKIPPED` instead of a result.

| Experiment | Question |
|---|---|
| A | Which process system calls does the shell issue, and in which order? |
| B | Does the shell wait for a foreground command? |
| C | Does the shell wait for a background command? |
| D | Which descriptor operations implement redirection? |
| E | Is a pipeline equivalent to a manual chain? |
| F | How does the shell report each class of failure? |
| G | Are zombies or file descriptors leaked? |

The methodology is documented in
[docs/experimental_methodology.md](docs/experimental_methodology.md).

---

## Using the project with VS Code on Windows

The project can be edited on Windows with Visual Studio Code, but it
cannot be *built* there: the source uses `fork(2)`, `execvp(3)`,
`sigaction(2)` and other POSIX interfaces that Windows does not provide.
There is no MinGW or Cygwin fallback in this project, and no native
Windows port is claimed.

The recommended workflow is:

1. **Edit on Windows.**  Open the repository folder in VS Code.  The
   `C/C++` extension provides syntax highlighting and IntelliSense.
   Because the POSIX headers are not present on Windows, IntelliSense will
   report the `unistd.h`, `sys/wait.h` and `signal.h` includes as
   unresolved; this is expected and does not affect the build on Linux.

2. **Build and test on Linux.**  Use one of the following, in order of
   preference:

   * **WSL2** - install Ubuntu from the Microsoft Store, then open the
     repository with `wslpath`:

     ```sh
     cd /mnt/c/Users/<user>/Documents/mini-unix-shell
     sudo apt install build-essential make strace procps
     make && make test
     ```

     The `fork(2)` system call works under WSL2, so the shell runs
     natively there.

   * **Docker Desktop** - mount the repository into a Linux container:

     ```sh
     docker run --rm -it -v "$PWD:/work" -w /work ubuntu:24.04 \
         bash -c "apt update && apt install -y build-essential make && make && make test"
     ```

   * **A physical Linux machine or a virtual machine** - clone the
     repository and run `make && make test`.

3. **Synchronise the files.**  The repository directory is shared between
   Windows and the Linux environment, so an edit made in VS Code on
   Windows is visible in WSL2 immediately and vice versa.  Nothing else
   needs to be copied.

The development of this project followed exactly this route: the sources
were written on Windows and every build, every test and every experiment
was executed in an Ubuntu 24.04 container on WSL2.

---

## Project structure

```
mini-unix-shell/
├── Makefile
├── README.md
├── LICENSE
├── .gitignore
├── src/                       the shell itself
│   ├── main.c                 entry point and options
│   ├── shell.c                interpreter loop, line reader, job table
│   ├── parser.c               tokeniser and parser
│   ├── builtins.c             help, pwd, cd, exit
│   ├── process.c              dispatch, child side, waitpid, reaper
│   ├── pipeline.c             pipes and descriptor plumbing
│   ├── redirection.c          open / dup / dup2 / close
│   ├── signals.c              sigaction, SIGINT and SIGCHLD
│   └── *.h                    the matching headers
├── tests/
│   ├── run_tests.sh           the driver
│   ├── testlib.sh             the shared helpers
│   ├── test_parser.c          unit tests for the parser (49 assertions)
│   ├── test_builtins.sh       (27 assertions)
│   ├── test_execution.sh      (38 assertions)
│   ├── test_redirection.sh    (19 assertions)
│   ├── test_pipelines.sh      (21 assertions)
│   ├── test_background.sh     (12 assertions)
│   └── test_signals.sh        (7 assertions)
├── experiments/
│   ├── run_all.sh             the driver
│   └── six experiment scripts
├── results/                   the transcripts of the last experiment run
└── docs/
    ├── report.md              the full academic report
    ├── parser_grammar.md      the grammar of the command language
    ├── architecture.md        module map and data flow
    ├── system_calls.md        every system call in detail
    ├── algorithms.md          the algorithms, step by step
    ├── limitations.md         what the shell does not do
    ├── experimental_methodology.md   how the experiments were carried out
    └── viva_questions.md      42 questions and answers
```

---

## Documentation

| Document | Contents |
|---|---|
| [docs/report.md](docs/report.md) | the academic report: motivation, design, implementation, tests, experiments, verification status |
| [docs/parser_grammar.md](docs/parser_grammar.md) | the complete grammar of the command language |
| [docs/architecture.md](docs/architecture.md) | the module map, the data flow, the dispatch policy, the descriptor ownership |
| [docs/system_calls.md](docs/system_calls.md) | every POSIX interface that is used, why it is used, and the Linux system call behind it |
| [docs/algorithms.md](docs/algorithms.md) | the line reader, the parser, the fork and exec sequence, the pipeline construction, the reaper |
| [docs/limitations.md](docs/limitations.md) | everything the shell does not do, and why |
| [docs/experimental_methodology.md](docs/experimental_methodology.md) | the verification environment, the method of every experiment, and the results |
| [docs/viva_questions.md](docs/viva_questions.md) | 42 questions with answers, for the oral examination |

---

## Platform support and verification status

| Platform | Status | Basis |
|---|---|---|
| Ubuntu 24.04 (container on WSL2) | **Verified** | clean build, 173 test assertions, 7 experiments |
| Ubuntu 24.04 (bare metal) | Not tested | no bare-metal machine was available |
| Kali Linux | Not tested | no Kali environment was available; the behaviour is expected to be identical |
| macOS, BSD, other Unix | Not tested | the program uses portable POSIX interfaces only |
| Windows (native) | Not supported | Windows provides neither `fork(2)` nor `execvp(3)` |

The verification environment in detail:

| Property | Value |
|---|---|
| Distribution (user space) | Ubuntu 24.04.5 LTS |
| Kernel | `Linux 6.6.87.2-microsoft-standard-WSL2 x86_64` |
| Compiler | gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0 |
| C library | glibc 2.39 |
| Build system | GNU Make 4.3 |
| Tracing tool | strace 6.8 |
| Container runtime | Docker Desktop 4.55.0, Linux containers on WSL2 |

The measurements were taken in an Ubuntu 24.04 **container on the WSL2
kernel**, not on a bare-metal Ubuntu installation.  The user space is
genuine Ubuntu 24.04; the kernel underneath is the WSL2 kernel.  This
distinction is stated wherever it matters, and
[docs/report.md](docs/report.md) section 26 marks every claim of the
report as *tested*, *not tested* or *not applicable*.

---

## Known limitations

The shell implements a deliberately small subset of the POSIX shell
language.  It has:

- no variable expansion, command substitution, pattern matching,
  here-documents, command separators or subshells - all of them are
  rejected with a diagnostic;
- no job control: no process groups, no `fg`, `bg`, `jobs` or `kill %1`,
  and no Ctrl+Z support;
- no line editing, no history and no tab completion;
- no aliases and no shell functions;
- no `cd -` and no `~` expansion;
- a fixed prompt (`minishell$ `) that is printed to standard error and
  only when standard input is a terminal.

The full list with the reason for each omission is in
[docs/limitations.md](docs/limitations.md).

---

## License

MIT License.  See [LICENSE](LICENSE).

The project is an academic case study.  It is provided as-is, without any
warranty of any kind.
#   M i n i U n i x S h e l l  
 