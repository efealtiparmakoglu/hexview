#!/usr/bin/env python3
"""Independent reference implementation of the hexview dump format.

The integration tests compare ./hexview output against this script so a
formatter bug cannot verify itself. Reads the input on stdin and prints the
expected dump on stdout.
"""
import argparse
import sys


def hexdump(data: bytes, width: int = 16, collapse: bool = True,
            base: int = 0) -> str:
    out = []
    prev = None
    starred = False
    i = 0
    while i < len(data):
        chunk = data[i:i + width]
        n = len(chunk)
        if collapse and prev is not None and prev == chunk:
            if not starred:
                out.append("*")
                starred = True
            i += n
            continue
        prev = chunk
        starred = False

        hexarea = []
        for j in range(width):
            if j > 0:
                hexarea.append("  " if j == width // 2 else " ")
            hexarea.append("%02x" % chunk[j] if j < n else "  ")
        ascii_col = "".join(chr(b) if 0x20 <= b <= 0x7E else "." for b in chunk)
        out.append("%08x  %s  |%s|" % (base + i, "".join(hexarea), ascii_col))
        i += n
    out.append("")
    return "\n".join(out) if len(out) > 1 else ""


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--cols", type=int, default=16)
    p.add_argument("--no-collapse", action="store_true")
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--length", type=int, default=None)
    args = p.parse_args()

    data = sys.stdin.buffer.read()
    if args.start:
        data = data[args.start:]
    if args.length is not None:
        data = data[:args.length]

    sys.stdout.write(hexdump(data, args.cols, not args.no_collapse, args.start))


if __name__ == "__main__":
    main()
