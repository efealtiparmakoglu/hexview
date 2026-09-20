#!/bin/sh
# Integration tests: hexview output is compared against tests/oracle.py, an
# independent reference implementation of the format.
set -eu
cd "$(dirname "$0")/.."
BIN=./hexview
TMP=tests/tmp
mkdir -p "$TMP"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

command -v python3 >/dev/null || {
    echo "python3 not found; skipping oracle-based integration tests" >&2
    exit 0
}

# ---------------------------------------------------------------- basics
$BIN --help >/dev/null || fail "--help"
$BIN --version >/dev/null || fail "--version"

printf 'The quick brown fox jumps over the lazy dog.' > "$TMP/text.bin"

# --------------------------------------------- oracle parity, many widths
for w in 1 3 4 8 15 16 17 31 64; do
    for mode in collapse no-collapse; do
        if [ "$mode" = collapse ]; then
            out=$($BIN --dump --color=never --cols "$w" "$TMP/text.bin")
            want=$(python3 tests/oracle.py --cols "$w" < "$TMP/text.bin")
        else
            out=$($BIN --dump --color=never --cols "$w" --no-collapse "$TMP/text.bin")
            want=$(python3 tests/oracle.py --cols "$w" --no-collapse < "$TMP/text.bin")
        fi
        [ "$out" = "$want" ] || fail "oracle mismatch (cols=$w $mode)"
    done
done

# ------------------------------------------------- binary corpus + folds
# bytes(range(16)) is exactly one line; repeating it 30 times forces the
# collapse path (30 identical consecutive lines -> line + "*").
python3 - "$TMP/repeat.bin" <<'EOF'
import sys
data = bytes(range(16)) * 30 + b"AB" * 10
open(sys.argv[1], "wb").write(data)
EOF
out=$($BIN --dump --color=never "$TMP/repeat.bin")
want=$(python3 tests/oracle.py < "$TMP/repeat.bin")
[ "$out" = "$want" ] || fail "oracle mismatch on binary corpus"

# ------------------------------------------------------------------ fuzz
i=0
while [ "$i" -lt 12 ]; do
    w=$(( (i % 5) * 7 + 1 ))
    head -c 5000 /dev/urandom > "$TMP/fuzz$i.bin"
    out=$($BIN --dump --color=never --cols "$w" "$TMP/fuzz$i.bin")
    want=$(python3 tests/oracle.py --cols "$w" < "$TMP/fuzz$i.bin")
    [ "$out" = "$want" ] || fail "fuzz mismatch (blob $i, cols $w)"
    rm -f "$TMP/fuzz$i.bin"
    i=$((i + 1))
done

# ----------------------------------------------------------------- stdin
printf 'stream me please' > "$TMP/stream.bin"
out=$(printf 'stream me please' | $BIN --dump --color=never -)
want=$($BIN --dump --color=never "$TMP/stream.bin")
[ "$out" = "$want" ] || fail "stdin output differs from file output"

# ------------------------------------------------------------- -s and -l
out=$($BIN --dump --color=never -s 13 -l 29 "$TMP/text.bin")
want=$(python3 tests/oracle.py --start 13 --length 29 < "$TMP/text.bin")
[ "$out" = "$want" ] || fail "-s/-l window mismatch"
out=$($BIN --dump --color=never -s 0x8 --length 4K "$TMP/text.bin")
want=$(python3 tests/oracle.py --start 8 --length 4096 < "$TMP/text.bin")
[ "$out" = "$want" ] || fail "suffix parsing mismatch"

# ------------------------------------------------------------ edge sizes
: > "$TMP/empty.bin"
out=$($BIN --dump --color=never "$TMP/empty.bin")
[ -z "$out" ] || fail "empty file produced output"
printf 'A' > "$TMP/one.bin"
out=$($BIN --dump --color=never "$TMP/one.bin")
want=$(python3 tests/oracle.py < "$TMP/one.bin")
[ "$out" = "$want" ] || fail "single byte mismatch"
head -c 32 /dev/zero > "$TMP/two-lines.bin"
out=$($BIN --dump --color=never --no-collapse "$TMP/two-lines.bin")
want=$(python3 tests/oracle.py --no-collapse < "$TMP/two-lines.bin")
[ "$out" = "$want" ] || fail "exact multiple mismatch"

# ------------------------------------------------------------ exit codes
if $BIN /nonexistent-file-xyz >/dev/null 2>&1; then
    fail "missing file must exit nonzero"
fi
status=0
$BIN /nonexistent-file-xyz >/dev/null 2>&1 || status=$?
[ "$status" -eq 1 ] || fail "missing file must exit 1, got $status"

status=0
$BIN --bunny "$TMP/text.bin" >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] || fail "bad option must exit 2, got $status"

status=0
$BIN -s notanumber "$TMP/text.bin" >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] || fail "bad offset must exit 2, got $status"

status=0
$BIN -c 0 "$TMP/text.bin" >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ] || fail "-c 0 must exit 2, got $status"

# start beyond EOF: silent success
out=$($BIN --dump --color=never -s 999999 "$TMP/text.bin")
[ -z "$out" ] || fail "beyond-EOF start produced output"

# ------------------------------------------------------------- collapse
folded=$($BIN --dump --color=never "$TMP/repeat.bin" | grep -c '^\*$' || true)
[ "$folded" -ge 1 ] || fail "repeated lines were not folded"
unfolded=$($BIN --dump --color=never --no-collapse "$TMP/repeat.bin" | grep -c '^\*$' || true)
[ "$unfolded" -eq 0 ] || fail "--no-collapse still folds"

# -------------------------------------------------------- sparse 4 GiB
# Exercises the 64-bit offset path without allocating real disk space.
if dd if=/dev/zero of="$TMP/sparse.bin" bs=4096 count=1 seek=1048575 2>/dev/null; then
    out=$($BIN --dump --color=never -s 0xFFFFF000 -l 16 "$TMP/sparse.bin")
    case "$out" in
        fffff000*) : ;;
        *) fail "sparse high offset wrong: $out" ;;
    esac
    rm -f "$TMP/sparse.bin"
else
    echo "note: sparse file unsupported here; skipping 4 GiB test" >&2
fi

rm -rf "$TMP"
echo "integration tests: all passed"
