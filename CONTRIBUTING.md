# Contributing to hexview

Thanks for your interest!

## Ground rules

- **Zero dependencies.** libc only — no ncurses, no external libraries, no
  code generation. The pager is deliberately hand-rolled on termios.
- **C11, no warnings.** The build uses `-Wall -Wextra -Wpedantic`; new code
  must compile clean. `make test-asan` must stay clean too.
- **Portability.** macOS and Linux are both first-class. Stick to POSIX
  interfaces (`pread`, `termios`, `ioctl`); guard anything beyond that.
- **No panics on input.** Malformed arguments exit with code 2, I/O failures
  with 1; the viewer never crashes on odd files, including huge or sparse
  ones.

## Workflow

```sh
make test        # unit + oracle-based integration tests
make test-asan   # same suite under sanitizers
```

The integration suite compares hexview against `tests/oracle.py`, an
independent implementation of the dump format. If you change the output
format, update the oracle and the golden expectations in the same commit —
and say why the format had to change.

## Pull requests

1. One feature or fix per PR.
2. Behavior changes need a test: formatter changes go through the oracle,
   key handling changes through a unit test where possible.
3. Match the existing style (C11, 4-space indent, no tabs except in the
   Makefile).

## Reporting bugs

Open an issue with the exact command line, your terminal emulator and size,
and the smallest file that reproduces the problem. Pager rendering bugs:
a screenshot or paste of the screen contents helps a lot.
