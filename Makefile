# Makefile for the "Building a Mini Unix Shell with Process System Calls"
# case study.
#
# The default target builds the `minishell` executable.  All compiler
# options can be overridden from the command line, e.g.
#
#     make CC=clang CFLAGS="-O3 -march=native"
#
# Targets:
#   all        build the shell (default)
#   run        build and start an interactive shell
#   test       build and run the complete test suite
#   debug      build with debug symbols and no optimisation
#   sanitize   build with AddressSanitizer and UndefinedBehaviorSanitizer
#   clean      remove all build artefacts
#   help       show this help text

# ---------------------------------------------------------------------
# Toolchain configuration
# ---------------------------------------------------------------------
# `CC` has a built-in default of `cc` in GNU Make.  Replace that default
# with `gcc` but keep an explicit `make CC=clang` or an exported CC
# variable working.
ifeq ($(origin CC), default)
CC := gcc
endif
CC       ?= gcc
STD      ?= -std=c11
WARNINGS ?= -Wall -Wextra -Wpedantic
OPT      ?= -O2
DEBUGFLAGS ?= -g
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
CFLAGS   ?= $(STD) $(WARNINGS) $(OPT) $(DEBUGFLAGS)
LDFLAGS  ?=
LDLIBS   ?=

# Extra flags used by the sanitizer build.
SANITIZER_FLAGS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

# ---------------------------------------------------------------------
# Directories and files
# ---------------------------------------------------------------------
SRCDIR   := src
TESTDIR  := tests
BUILDDIR := build
OBJDIR   := $(BUILDDIR)/obj

TARGET   := minishell

SOURCES  := $(SRCDIR)/main.c \
            $(SRCDIR)/shell.c \
            $(SRCDIR)/parser.c \
            $(SRCDIR)/builtins.c \
            $(SRCDIR)/process.c \
            $(SRCDIR)/pipeline.c \
            $(SRCDIR)/redirection.c \
            $(SRCDIR)/signals.c

OBJECTS  := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))
DEPS     := $(OBJECTS:.o=.d)

TEST_BIN := $(BUILDDIR)/test_parser

.PHONY: all run test debug sanitize sanitize-test clean help

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ---------------------------------------------------------------------
# Unit tests
# ---------------------------------------------------------------------
$(TEST_BIN): $(TESTDIR)/test_parser.c $(OBJDIR)/parser.o | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(SRCDIR) -o $@ $^ $(LDLIBS)

# ---------------------------------------------------------------------
# High level targets
# ---------------------------------------------------------------------
run: $(TARGET)
	@./$(TARGET)

test: $(TARGET) $(TEST_BIN)
	@$(TESTDIR)/run_tests.sh

debug:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory OPT="-O0" DEBUGFLAGS="-g3 -DDEBUG" \
	         CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -O0 -g3 -DDEBUG" \
	         all
	@echo "debug build finished: ./$(TARGET)"

sanitize:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory \
	         OPT="-O1" \
	         DEBUGFLAGS="-g3" \
	         CFLAGS="$(STD) $(WARNINGS) -O1 -g3 $(SANITIZER_FLAGS)" \
	         LDFLAGS="$(SANITIZER_FLAGS)" \
	         all $(TEST_BIN)
	@echo "sanitizer build finished: ./$(TARGET)"

sanitize-test: sanitize
	@echo "== running the test suite under AddressSanitizer / UBSan =="
	@ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1 \
	 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
	 $(TESTDIR)/run_tests.sh

clean:
	@rm -rf $(BUILDDIR) $(TARGET)
	@echo "clean: build artefacts removed"

help:
	@echo "Available targets:"
	@echo "  all           build ./$(TARGET)                (default)"
	@echo "  run           build and start the shell"
	@echo "  test          build and run the test suite"
	@echo "  debug         rebuild with -O0 -g3 -DDEBUG"
	@echo "  sanitize      rebuild with ASan and UBSan"
	@echo "  sanitize-test rebuild with the sanitizers and run the tests"
	@echo "  clean         remove build artefacts"
	@echo "  help          show this text"

-include $(DEPS)
