/* hexdump.c — see hexdump.h.
 *
 * Line layout (width 16, hexdump -C compatible):
 *   00000000  54 68 65 20 71 75 69 63  6b 20 62 72 6f 77 6e 20  |The quick brown |
 * Partial lines pad the hex area with spaces so the ASCII column stays
 * aligned. Offsets are zero-padded to 8 digits and grow wider beyond 4 GiB.
 */
#include "hexdump.h"

#include <inttypes.h>
#include <string.h>

#include "util.h"

#define LINE_CAP 8192

static const char HEXDIGITS[] = "0123456789abcdef";

static size_t append(char *dst, size_t cap, size_t pos, const char *s, size_t n) {
    if (pos + n > cap)
        return cap + 1; /* signal overflow */
    memcpy(dst + pos, s, n);
    return pos + n;
}

static size_t append_color(char *dst, size_t cap, size_t pos, const char *code) {
    if (!code)
        return pos;
    return append(dst, cap, pos, code, strlen(code));
}

long hv_format_line(char *dst, size_t cap, const uint8_t *bytes, size_t n,
                    uint64_t offset, const hv_dump_opts *o) {
    char tmp[LINE_CAP];
    size_t pos = 0;
    int w = o->width;

    /* Offset field: at least 8 digits, wider when the address demands it. */
    int digits = 1;
    for (uint64_t v = offset; v >= 0x10; v >>= 4)
        digits++;
    if (digits < 8)
        digits = 8;

    if (o->color)
        pos = append_color(tmp, sizeof tmp, pos, HV_COLOR_OFFSET);
    {
        char offbuf[24];
        int written = snprintf(offbuf, sizeof offbuf, "%0*llx", digits,
                               (unsigned long long)offset);
        if (written < 0 || (size_t)written >= sizeof offbuf)
            return -1;
        pos = append(tmp, sizeof tmp, pos, offbuf, (size_t)written);
    }
    if (o->color)
        pos = append(tmp, sizeof tmp, pos, HV_COLOR_RESET, 4);
    pos = append(tmp, sizeof tmp, pos, "  ", 2);

    /* Hex area: two groups separated by an extra space, padded when short. */
    for (int i = 0; i < w; i++) {
        if (i > 0)
            pos = append(tmp, sizeof tmp, pos, (i == w / 2) ? "  " : " ", (i == w / 2) ? 2 : 1);
        if (i < (int)n) {
            uint8_t b = bytes[i];
            char hex[2] = {HEXDIGITS[b >> 4], HEXDIGITS[b & 0x0F]};
            if (o->color)
                pos = append_color(tmp, sizeof tmp, pos, HV_CLASS_COLOR[hv_classify(b)]);
            pos = append(tmp, sizeof tmp, pos, hex, 2);
            if (o->color)
                pos = append_color(tmp, sizeof tmp, pos, HV_COLOR_RESET);
        } else {
            pos = append(tmp, sizeof tmp, pos, "  ", 2);
        }
    }
    pos = append(tmp, sizeof tmp, pos, "  |", 3);

    for (size_t i = 0; i < n; i++) {
        uint8_t b = bytes[i];
        char ch;
        const char *color = NULL;
        if (b >= 0x20 && b <= 0x7E) {
            ch = (char)b;
            color = o->color ? HV_CLASS_COLOR[CL_PRINT] : NULL;
        } else {
            ch = '.';
            color = o->color ? HV_CLASS_COLOR[CL_ZERO] : NULL;
        }
        pos = append_color(tmp, sizeof tmp, pos, color);
        pos = append(tmp, sizeof tmp, pos, &ch, 1);
        if (color)
            pos = append_color(tmp, sizeof tmp, pos, HV_COLOR_RESET);
    }
    pos = append(tmp, sizeof tmp, pos, "|\n", 2);

    if (pos + 1 > cap)
        return -1;
    memcpy(dst, tmp, pos);
    dst[pos] = '\0';
    return (long)pos;
}

/* Collapse state for the dump engines. */
typedef struct {
    uint8_t prev[HV_MAX_WIDTH];
    size_t prev_n;
    int starred;
} collapse_state;

static void collapse_init(collapse_state *cs) {
    cs->prev_n = (size_t)-1;
    cs->starred = 0;
}

/* Returns 1 if this line should be suppressed (it repeats and is folded). */
static int collapse_check(collapse_state *cs, FILE *out, const uint8_t *bytes,
                          size_t n) {
    if (cs->prev_n == n && n > 0 && memcmp(cs->prev, bytes, n) == 0) {
        if (!cs->starred) {
            fputs("*\n", out);
            cs->starred = 1;
        }
        return 1;
    }
    memcpy(cs->prev, bytes, n);
    cs->prev_n = n;
    cs->starred = 0;
    return 0;
}

int hv_dump_fd(FILE *out, int fd, uint64_t start, uint64_t len_limit,
               const hv_dump_opts *o) {
    static uint8_t buf[1 << 16];
    char line[LINE_CAP];
    collapse_state cs;
    uint64_t off = start;
    uint64_t remaining = len_limit;

    collapse_init(&cs);
    while (remaining > 0) {
        size_t want = sizeof buf;
        if ((uint64_t)want > remaining)
            want = (size_t)remaining;
        size_t got;
        if (hv_pread_full(fd, buf, want, off, &got) != 0)
            return -1;
        if (got == 0)
            break;

        size_t pos = 0;
        while (pos < got) {
            size_t n = (size_t)o->width <= (got - pos) ? (size_t)o->width : got - pos;
            if (!(o->collapse && collapse_check(&cs, out, buf + pos, n))) {
                long len = hv_format_line(line, sizeof line, buf + pos, n, off, o);
                if (len < 0)
                    return -1;
                fwrite(line, 1, (size_t)len, out);
            }
            off += n;
            pos += n;
            remaining -= n;
        }
        if (got < want)
            break; /* EOF */
    }
    return 0;
}

int hv_dump_stream(FILE *out, FILE *in, uint64_t start, const hv_dump_opts *o) {
    static uint8_t buf[1 << 16];
    static uint8_t carry[HV_MAX_WIDTH];
    char line[LINE_CAP];
    collapse_state cs;
    size_t carry_n = 0;
    uint64_t off = start;

    collapse_init(&cs);
    for (;;) {
        size_t got = fread(buf, 1, sizeof buf, in);
        if (got == 0)
            break;
        if (ferror(in))
            return -1;

        /* Prepend any bytes held back from the previous chunk. */
        static uint8_t chunk[2 * (1 << 16) + HV_MAX_WIDTH];
        memcpy(chunk, carry, carry_n);
        memcpy(chunk + carry_n, buf, got);
        size_t total = carry_n + got;

        size_t pos = 0;
        while (total - pos >= (size_t)o->width) {
            if (!(o->collapse && collapse_check(&cs, out, chunk + pos, (size_t)o->width))) {
                long len = hv_format_line(line, sizeof line, chunk + pos,
                                          (size_t)o->width, off, o);
                if (len < 0)
                    return -1;
                fwrite(line, 1, (size_t)len, out);
            }
            pos += (size_t)o->width;
            off += (size_t)o->width;
        }
        carry_n = total - pos;
        memmove(carry, chunk + pos, carry_n);
        if (feof(in))
            break;
    }

    if (carry_n > 0) {
        long len = hv_format_line(line, sizeof line, carry, carry_n, off, o);
        if (len < 0)
            return -1;
        fwrite(line, 1, (size_t)len, out);
    }
    return 0;
}
