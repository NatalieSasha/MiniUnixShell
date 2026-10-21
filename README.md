# Mini Unix Shell

`minishell` is a Unix command interpreter written from scratch in C11. It reads a command line, parses it according to an explicitly documented grammar, and executes it using POSIX process interfaces such as `fork(2)`, `execvp(3)`, `waitpid(2)`, `pipe(2)`, `dup2(2)`, `open(2)`, `close(2)` and `sigaction(2)`.

The project is a case study in the Unix process model. Its purpose is to make the mechanisms behind a shell observable and verifiable: claims about process creation, redirection, pipelines, background execution and signal handling are backed by automated tests or stored system call traces.

**Development environment:** Windows with VS Code  
**Execution environment:** Ubuntu 24.04 on WSL2  
**Language:** C11  
**Build system:** GNU Make

The project uses POSIX interfaces and does not run natively on Windows.

## Table of Contents

1. [Features](#features)
2. [Example Session](#example-session)
3. [Requirements](#requirements)
4. [Installation on Ubuntu](#installation-on-ubuntu)
5. [Installation on Kali Linux](#installation-on-kali-linux)
6. [Building](#building)
7. [Running the Shell](#running-the-shell)
8. [Supported Command Syntax](#supported-command-syntax)
9. [Built-in Commands](#built-in-commands)
10. [Exit Status Conventions](#exit-status-conventions)
11. [Makefile Targets](#makefile-targets)
12. [Running the Tests](#running-the-tests)
13. [Running the Sanitizers](#running-the-sanitizers)
14. [Running the Experiments](#running-the-experiments)
15. [Using VS Code on Windows](#using-vs-code-on-windows)
16. [Project Structure](#project-structure)
17. [Documentation](#documentation)
18. [Platform Support and Verification](#platform-support-and-verification)
19. [Known Limitations](#known-limitations)
20. [License](#license)

---

## Features

- Four built-in commands: `help`, `pwd`, `cd` and `exit`.
- Single quotes, double quotes and backslash escaping.
- Input redirection (`<`).
- Output redirection (`>`).
- Append redirection (`>>`).
- Pipelines containing multiple stages (`|`).
- Background execution (`&`) with a job table and completion notifications.
- POSIX-style exit status conventions: `0`, `1`, `2`, `126`, `127` and `128 + signal`.
- Explicit rejection of unsupported syntax with diagnostic messages.
- Automated testing with 173 assertions across seven test programs.
- AddressSanitizer and UndefinedBehaviorSanitizer testing.
- Seven experiments using `strace(1)` to examine system calls and process behaviour.
- Stored experiment transcripts in the `results/` directory.

## Example Session

```text
minishell$ pwd
/home/student/mini-unix-shell

minishell$ help
minishell 1.0.0 - a small Unix-like shell

Built-in commands:
  help              show this help text
  pwd               print the current working directory
  cd [directory]    change the current working directory
                    (without an argument, changes to $HOME)
  exit [status]     leave the shell; status is an integer 0-255

Command syntax:
  cmd args...            run an external program
  cmd | cmd | cmd        connect commands with a pipe
  cmd < file             read standard input from a file
  cmd > file             write standard output to a file
  cmd >> file            append standard output to a file
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

The process ID in the background example is illustrative.

## Requirements

| Requirement | Version used for verification |
|---|---|
| POSIX-compatible operating system | Ubuntu 24.04 user space |
| C11 compiler | GCC 13.3.0 |
| GNU Make | 4.3 |
| POSIX shell | Bash or Dash |
| `strace` (optional) | 6.8 |
| `procps` (optional) | Required for Experiment G |

No third-party libraries are required. The shell links only against the C library.

## Installation on Ubuntu

Update the package lists and install the required tools:

```bash
sudo apt update
sudo apt install build-essential make strace procps
```

Navigate to the repository directory:

```bash
cd mini-unix-shell
```

Build the project:

```bash
make
```

`build-essential` provides GCC and the standard headers. `make` is required for building the project, while `strace` and `procps` are needed for the experiments.

If you only want to build the shell and run the tests, `build-essential` and `make` are sufficient.

## Installation on Kali Linux

Kali Linux is Debian-based and uses the same package names.

Install the required packages:

```bash
sudo apt update
sudo apt install build-essential make strace procps
```

Navigate to the repository directory and build the project:

```bash
cd mini-unix-shell
make
```

**Verification note:** The project was not executed on Kali Linux during development. All measurements were taken in an Ubuntu 24.04 environment. Kali is expected to behave similarly because it uses the same POSIX interfaces, but this has not been independently tested.

See [docs/report.md](docs/report.md), Section 26.3, for the platform verification status.

## Building

Build the project using:

```bash
make
```

This produces the executable:

```text
./minishell
```

The default compiler options are:

```text
-std=c11 -Wall -Wextra -Wpedantic -O2 -g
-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
```

The build is warning-free with GCC 13 in the documented verification environment.

You can override the compiler and compiler flags:

```bash
make CC=clang
make CFLAGS="-std=c11 -Wall -Wextra -O3"
```

## Running the Shell

Start an interactive shell using:

```bash
make run
```

Alternatively, after building the project:

```bash
./minishell
```

The shell can also read commands from standard input, making it suitable for scripted tests:

```bash
printf 'ls -l | wc -l\nexit 0\n' | ./minishell
```

The following command-line options are supported:

| Option | Description |
|---|---|
| `-h`, `--help` | Display the help text and exit |
| `-v`, `--version` | Display the version and exit |

## Supported Command Syntax

The complete command grammar is documented in [docs/parser_grammar.md](docs/parser_grammar.md).

| Syntax | Example | Meaning |
|---|---|---|
| Simple command | `ls -l` | Execute an external program |
| Single quotes | `echo 'a  b'` | Treat characters literally |
| Double quotes | `echo "a  b"` | Preserve spaces; support `\"` and `\\` escapes |
| Backslash | `echo a\ b` | Escape the next character |
| Empty argument | `echo ""` | Pass an empty string as an argument |
| Adjacent quoting | `echo a"b c"d` | Produce one argument: `ab cd` |
| Input redirection | `cat < file` | Read standard input from a file |
| Output redirection | `ls > file` | Write standard output to a file, truncating it |
| Append redirection | `ls >> file` | Append standard output to a file |
| Pipeline | `cat f \| grep x \| wc -l` | Connect commands using pipes |
| Background execution | `sleep 10 &` | Execute without waiting for completion |

Redirection binds more tightly than the pipe operator. For example:

```bash
cat < in.txt | grep error > out.txt
```

is interpreted as:

```text
( cat < in.txt ) | ( grep error > out.txt )
```

### Unsupported Syntax

The following constructs are not supported and are rejected with the diagnostic `unsupported shell syntax`:

```bash
echo $HOME          # Variable expansion
echo `date`         # Command substitution
echo $(date)        # Command substitution
ls *.c              # Pattern matching
cat << EOF          # Here-documents
echo a; echo b      # Command separators
(echo a)            # Subshells and grouping
```

These constructs are deliberately rejected rather than silently misinterpreted. For example, a shell that silently ignored `*.c` might execute `ls` on a file literally named `*.c`, which differs from the user's intended command.

To pass such text literally, use quotes:

```bash
echo '$HOME'
```

## Built-in Commands

| Command | Description | Arguments |
|---|---|---|
| `help` | Displays the built-in reference and syntax summary | None |
| `pwd` | Prints the current working directory | None |
| `cd [dir]` | Changes the working directory; without an argument, changes to `$HOME` | Zero or one |
| `exit [status]` | Exits the shell with a specified status, from 0 to 255; defaults to 0 | Zero or one |

The `pwd` command prints the physical path using `getcwd(3)`, which resolves symbolic links. Logical path handling, such as `pwd -L`, is not implemented.

## Exit Status Conventions

| Status | Meaning |
|---|---|
| `0` | Success |
| `1` | General failure, such as a nonexistent directory |
| `2` | Incorrect use of a built-in command |
| `126` | Command found but could not be executed |
| `127` | Command not found |
| `128 + n` | Command terminated by signal `n` |
| `130` | Interrupted by `SIGINT` (`128 + 2`) |
| `137` | Killed by `SIGKILL` (`128 + 9`) |

The exit status of a pipeline is the status of its last stage, following conventional POSIX shell behaviour.

A background job does not change the shell's exit status.

## Makefile Targets

| Target | Effect |
|---|---|
| `make` or `make all` | Build `./minishell` |
| `make run` | Build and start an interactive shell |
| `make test` | Build and run the complete test suite |
| `make debug` | Rebuild with `-O0 -g3 -DDEBUG` |
| `make sanitize` | Rebuild with AddressSanitizer and UndefinedBehaviorSanitizer |
| `make sanitize-test` | Rebuild with sanitizers and run the test suite |
| `make clean` | Remove build artifacts |
| `make help` | Display the available Makefile targets |

## Running the Tests

Run the complete test suite:

```bash
make test
```

The suite consists of one parser unit-test program and six integration-test scripts that execute the compiled shell.

Each script creates its own temporary directory, removes it after execution and uses `timeout(1)` to prevent hanging tests from blocking the suite indefinitely.

The expected successful output concludes with a summary similar to:

```text
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

One assertion in `test_redirection.sh` is skipped when running as `root`, because the root user may write to a file whose permissions are set to `0444`. The skip is reported explicitly.

## Running the Sanitizers

Run the full test suite with sanitizers:

```bash
make sanitize-test
```

The shell is rebuilt with:

```text
-fsanitize=address,undefined -fno-omit-frame-pointer
```

The test suite uses the following environment settings:

```bash
ASAN_OPTIONS=detect_leaks=1
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
```

The documented verification run passed without sanitizer reports.

## Running the Experiments

Build the shell and run all experiments:

```bash
make
experiments/run_all.sh
```

The experiment scripts use `strace(1)`, `ps(1)` and `date(1)`.

Each experiment writes a transcript to `results/<name>.txt`. The combined log is stored in:

```text
results/experiments.txt
```

If `strace(1)` is unavailable, the corresponding experiment reports `SKIPPED`.

### Experiment Overview

| Experiment | Research question |
|---|---|
| A | Which process-related system calls does the shell issue, and in what order? |
| B | Does the shell wait for a foreground command? |
| C | Does the shell wait for a background command? |
| D | Which file-descriptor operations implement redirection? |
| E | Is a pipeline equivalent to a manually constructed chain? |
| F | How does the shell report different classes of failure? |
| G | Are zombie processes or file descriptors leaked? |

The methodology is documented in [docs/experimental_methodology.md](docs/experimental_methodology.md).

## Using VS Code on Windows

The project can be edited on Windows using Visual Studio Code, but it cannot be built natively on Windows because it uses POSIX interfaces such as `fork(2)`, `execvp(3)` and `sigaction(2)`.

The project does not provide a MinGW or Cygwin fallback and does not claim native Windows support.

### 1. Edit on Windows

Open the repository folder in VS Code.

The C/C++ extension provides syntax highlighting and IntelliSense. Since POSIX headers are not available in a standard Windows environment, IntelliSense may report unresolved includes such as:

- `unistd.h`
- `sys/wait.h`
- `signal.h`

These warnings are expected when editing outside a Linux environment and do not affect a successful Linux build.

### 2. Build and Test on Linux

The recommended options are:

**Option A: Windows Subsystem for Linux (WSL2)**

Install Ubuntu through the Microsoft Store, then navigate to the repository from the Linux terminal.

For a repository located under the Windows Documents directory, the path may resemble:

```bash
cd /mnt/c/Users/<user>/Documents/mini-unix-shell
```

Install the dependencies and run the build and tests:

```bash
sudo apt update
sudo apt install build-essential make strace procps
make
make test
```

WSL2 supports the required `fork(2)` system call.

**Option B: Docker Desktop**

Mount the repository into an Ubuntu Linux container:

```bash
docker run --rm -it -v "$PWD:/work" -w /work ubuntu:24.04 \
  bash -c "apt update && apt install -y build-essential make && make && make test"
```

**Option C: Physical Linux Machine or Virtual Machine**

Clone the repository onto a Linux machine or virtual machine, install the dependencies and run:

```bash
make
make test
```

### 3. Synchronise Files

When the repository is stored in a Windows directory accessible through WSL2, edits made in VS Code are visible in the Linux environment and vice versa.

The project was developed using Windows for editing, with builds, tests and experiments executed in an Ubuntu 24.04 container running on WSL2.

## Project Structure

```text
mini-unix-shell/
├── Makefile
├── README.md
├── LICENSE
├── .gitignore
├── src/
│   ├── main.c
│   ├── shell.c
│   ├── parser.c
│   ├── builtins.c
│   ├── process.c
│   ├── pipeline.c
│   ├── redirection.c
│   ├── signals.c
│   └── *.h
├── tests/
│   ├── run_tests.sh
│   ├── testlib.sh
│   ├── test_parser.c
│   ├── test_builtins.sh
│   ├── test_execution.sh
│   ├── test_redirection.sh
│   ├── test_pipelines.sh
│   ├── test_background.sh
│   └── test_signals.sh
├── experiments/
│   ├── run_all.sh
│   └── experiment scripts
├── results/
│   └── experiment transcripts
└── docs/
    ├── report.md
    ├── parser_grammar.md
    ├── architecture.md
    ├── system_calls.md
    ├── algorithms.md
    ├── limitations.md
    ├── experimental_methodology.md
    └── viva_questions.md
```

### Source Code Overview

| File | Responsibility |
|---|---|
| `src/main.c` | Program entry point and command-line options |
| `src/shell.c` | Interpreter loop, line reader and job table |
| `src/parser.c` | Tokenisation and command parsing |
| `src/builtins.c` | Implementation of built-in commands |
| `src/process.c` | Process dispatch, child execution, waiting and reaping |
| `src/pipeline.c` | Pipeline creation and file-descriptor management |
| `src/redirection.c` | File opening and descriptor duplication |
| `src/signals.c` | Signal handling using `sigaction`, `SIGINT` and `SIGCHLD` |

## Documentation

The repository includes detailed documentation covering implementation, algorithms, verification and limitations.

| Document | Contents |
|---|---|
| [docs/report.md](docs/report.md) | Academic report covering motivation, design, implementation, tests, experiments and verification status |
| [docs/parser_grammar.md](docs/parser_grammar.md) | Complete grammar of the command language |
| [docs/architecture.md](docs/architecture.md) | Module map, data flow, dispatch policy and descriptor ownership |
| [docs/system_calls.md](docs/system_calls.md) | POSIX interfaces used, their purpose and associated Linux system calls |
| [docs/algorithms.md](docs/algorithms.md) | Line reading, parsing, process creation, execution, pipeline construction and child reaping |
| [docs/limitations.md](docs/limitations.md) | Unsupported functionality and the reasons for each omission |
| [docs/experimental_methodology.md](docs/experimental_methodology.md) | Verification environment, experimental methods and results |
| [docs/viva_questions.md](docs/viva_questions.md) | 42 questions and answers for oral examination preparation |

## Platform Support and Verification

| Platform | Status | Basis |
|---|---|---|
| Ubuntu 24.04 in a container on WSL2 | **Verified** | Clean build, 173 test assertions and seven experiments |
| Ubuntu 24.04 on bare metal | Not tested | No bare-metal machine was available |
| Kali Linux | Not tested | Behaviour is expected to be similar but has not been verified |
| macOS, BSD and other Unix systems | Not tested | Uses POSIX interfaces, but compatibility has not been independently verified |
| Native Windows | Not supported | Required interfaces such as `fork(2)` and `execvp(3)` are unavailable natively |

### Verification Environment

| Property | Value |
|---|---|
| Distribution | Ubuntu 24.04.5 LTS |
| Kernel | `Linux 6.6.87.2-microsoft-standard-WSL2 x86_64` |
| Compiler | GCC 13.3.0 |
| C library | glibc 2.39 |
| Build system | GNU Make 4.3 |
| Tracing tool | `strace` 6.8 |
| Container runtime | Docker Desktop 4.55.0 using Linux containers on WSL2 |

The measurements were collected in an Ubuntu 24.04 container running on the WSL2 kernel, not on a bare-metal Ubuntu installation.

The user space is genuine Ubuntu 24.04, while the underlying kernel is provided by WSL2. The distinction is documented in [docs/report.md](docs/report.md), Section 26, where claims are identified as tested, not tested or not applicable.

## Known Limitations

The shell deliberately implements a small subset of the POSIX shell language.

It does not currently support:

- Variable expansion or command substitution.
- Pattern matching and globbing.
- Here-documents.
- Command separators and subshells.
- Job control, process groups, `fg`, `bg`, `jobs` or `kill %1`.
- `Ctrl+Z` support.
- Command-line editing, command history or tab completion.
- Aliases or shell functions.
- `cd -` or tilde (`~`) expansion.
- Logical path handling such as `pwd -L`.

Unsupported syntax is rejected with a diagnostic rather than silently interpreted.

The shell also uses a fixed prompt, `minishell$ `, printed to standard error only when standard input is a terminal.

The complete explanation of each limitation is available in [docs/limitations.md](docs/limitations.md).

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

The project is provided as an academic case study, as-is and without warranty of any kind.
