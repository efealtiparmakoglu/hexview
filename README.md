# hexview

**A fast, colorful, less-style hex viewer in pure C — zero dependencies beyond libc.**

[![CI](https://github.com/efealtiparmakoglu/hexview/actions/workflows/ci.yml/badge.svg)](https://github.com/efealtiparmakoglu/hexview/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C11](https://img.shields.io/badge/standard-C11-blue.svg)]()

`hexview` dumps files in the canonical `hexdump -C` layout, colors them by
byte class, folds repeated lines, and — when you open a file directly in a
terminal — becomes a full-screen pager with vi keys, hex search and offset
jumping.

```
00000000  54 68 65 20 71 75 69 63  6b 20 62 72 6f 77 6e 20  |The quick brown |
00000010  66 6f 78 20 6a 75 6d 70  73 20 6f 76 65 72 20 74  |fox jumps over t|
00000020  68 65 20 6c 61 7a 79 20  64 6f 67                 |he lazy dog|
```

## Why hexview

- **Zero dependencies** — no ncurses, no autotools, no submodules. It builds
  with `make` against libc on macOS and Linux.
- **Interactive without ncurses** — the pager is hand-rolled on termios and
  ANSI escapes: alternate screen, per-frame viewport repaint, live status bar.
- **Search and navigation** — jump to any 64-bit offset, search for hex byte
  patterns (`5a 41 52`), repeat matches with `n`/`N`, wrap included.
- **Large files are the default case** — everything is `pread`-based with
  64-bit offsets; a 4 GiB sparse file opens instantly and only the viewport
  is ever read.
- **Independent test oracle** — the integration tests compare output against
  `tests/oracle.py`, a separate reference implementation of the format, so a
  formatter bug cannot verify itself. Sanitizer runs are part of the suite.
- **Familiar defaults** — dump output is hexdump -C compatible (offset, two
  8-byte hex groups, `|ascii|` column, `*` line folding), pipe it anywhere.

## Install

```sh
make            # builds ./hexview
make install    # PREFIX=/usr/local by default
```

Requires any C11 compiler. No other prerequisites.

## Usage

```sh
hexview file.bin              # terminal: interactive pager
hexview file.bin | less       # piped: plain dump (auto-detected)
hexview --dump -s 0x1F00 file.bin
head -c 1000 file | hexview -
```

| Option | Meaning |
|---|---|
| `-c, --cols <N>` | bytes per line, 1–64 (default 16) |
| `-s, --start <OFF>` | start offset — decimal, `0x` hex, `K`/`M`/`G` suffix |
| `-l, --length <N>` | limit output to N bytes |
| `-i, --interactive` | force the pager |
| `--dump` | force plain dump mode |
| `--color auto\|always\|never` | ANSI coloring (default: auto) |
| `--no-collapse` | keep repeated lines instead of folding with `*` |

### Pager keys

| Key | Action |
|---|---|
| `j` / `k`, arrows | line down / up |
| `d`, `u`, PgDn, PgUp, Space, `b` | half page down / up |
| `g` | goto offset (hex prompt; empty = top) |
| `G` | jump to end of file |
| `/` | search a hex byte pattern, e.g. `5a 41 52` |
| `n` / `N` | next / previous match (wraps around) |
| `r`, Ctrl-L | redraw |
| `?` | help overlay |
| `q`, Ctrl-C | quit |

Colors: NUL bytes are dim, printable ASCII is green, other bytes are red,
offsets are bold blue. Byte classes make binary structure readable at a
glance — padding, string runs and control regions are visually distinct.

## Performance

Release build, Apple silicon; `xxd` measured on the same machine for scale.

| Task | hexview | xxd |
|---|---|---|
| dump 100 MiB random binary | 1.25 s (~84 MiB/s) | 2.43 s |
| dump 32 MiB text (with folding) | 0.28 s (~120 MiB/s) | — |

The interactive pager reads only the visible window, so opening position and
file size are independent: a 4 GiB sparse file opens instantly and seeking is
a single `pread` regardless of distance.

## Development

```sh
make test        # unit suite + oracle-driven integration tests
make test-asan   # the same suite under ASan + UBSan
```

- **Unit tests** (`src/tests.c`): offset/pattern parsers, exact line
  formatting (plain and colored), collapse state machine, stream carry
  across read boundaries, `-s/-l` windows.
- **Integration tests** (`tests/run.sh`): oracle parity across widths
  1–64, random fuzz blobs, stdin/file equivalence, exit codes, sparse
  4 GiB offsets.
- The oracle (`tests/oracle.py`) is an independent implementation of the
  dump format; the tests never compare hexview against itself.

## License

MIT — see [LICENSE](LICENSE).
