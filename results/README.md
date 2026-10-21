# Experimental results

This directory holds the transcripts of the last complete run of the
experiment suite.  They are the primary evidence for the claims made in
[../docs/report.md](../docs/report.md) and in
[../docs/experimental_methodology.md](../docs/experimental_methodology.md).

## How to regenerate them

```sh
make
experiments/run_all.sh
```

The scripts require `strace(1)` (for the traces) and `procps` (for the
process table in Experiment G).  If `strace(1)` is missing, the affected
experiment prints `SKIPPED` instead of a result.

## The files

| File | Experiment | What it shows |
|---|---|---|
| `process_creation.txt` | A | the `fork` / `exec` / `wait` sequence of an external command, as observed with `strace -f -e trace=process` |
| `foreground_background.txt` | B, C | the wall-clock time of a foreground `sleep 3` and of a background `sleep 3 &`, and the job table notification |
| `syscall_trace.txt` | D | the `openat` / `dup2` / `close` sequence that implements `<` and `>` |
| `pipeline_experiment.txt` | E | a pipeline compared with the equivalent manual chain, the `pipe2` / `dup2` sequence, and the transfer time of 200000 lines |
| `error_handling.txt` | F | the diagnostic and the exit status of every class of failure, and the survival of the shell |
| `resource_management.txt` | G | the absence of zombies after ten background jobs and the absence of descriptor leaks |
| `experiments.txt` | all | the combined log of the whole run, with the environment header |

## How to read these files

Each file begins with a header that records the date (UTC), the kernel, the
compiler and the version of the shell, so that a transcript can always be
traced back to the environment that produced it.

Two kinds of value in the transcripts are **not** stable between runs and
must not be quoted as fixed numbers:

* **process ids** - assigned by the kernel; they differ from run to run;
* **wall-clock durations** - they depend on the machine and on the load.

Everything else in the transcripts - the *sequence* of the system calls,
the diagnostic messages, the exit statuses, the number of open descriptors
- is deterministic and is the actual result.

The `results/` directory is tracked in the repository on purpose: the
transcripts are the evidence for the report and should be readable without
re-running the experiments.
