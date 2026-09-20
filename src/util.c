/* util.c — see util.h. */
#include "util.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

const char *const HV_CLASS_COLOR[3] = {
    "\x1b[2m",   /* CL_ZERO  */
    "\x1b[32m",  /* CL_PRINT */
    "\x1b[31m",  /* CL_OTHER */
};

byte_class hv_classify(uint8_t b) {
    if (b == 0x00)
        return CL_ZERO;
    if (b >= 0x20 && b <= 0x7E)
        return CL_PRINT;
    return CL_OTHER;
}

int hv_parse_u64(const char *s, uint64_t *out) {
    if (s == NULL || *s == '\0')
        return -1;

    while (*s == ' ' || *s == '\t')
        s++;

    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == '\0')
            return -1;
    }

    uint64_t v = 0;
    int digits = 0;
    for (; *s != '\0'; s++, digits++) {
        int d;
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            break;
        if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base)
            return -1;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    if (digits == 0)
        return -1;

    /* Optional binary suffix: K, KiB, M, MiB, G, GiB. */
    if (*s == 'k' || *s == 'K' || *s == 'm' || *s == 'M' || *s == 'g' || *s == 'G') {
        uint64_t mul = 0;
        switch (*s) {
        case 'k':
        case 'K':
            mul = 1024ull;
            break;
        case 'm':
        case 'M':
            mul = 1024ull * 1024;
            break;
        default:
            mul = 1024ull * 1024 * 1024;
            break;
        }
        s++;
        if (s[0] == 'i' && (s[1] == 'b' || s[1] == 'B'))
            s += 2;
        if (v != 0 && mul > UINT64_MAX / v)
            return -1;
        v *= mul;
    }

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s != '\0')
        return -1;

    *out = v;
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int hv_parse_hex(const char *s, uint8_t *buf, size_t cap, size_t *out_len) {
    if (s == NULL || buf == NULL || out_len == NULL)
        return -1;

    uint8_t cur = 0;
    int have_nibble = 0;
    size_t len = 0;

    for (; *s != '\0'; s++) {
        if (*s == ' ' || *s == '\t')
            continue;
        int v = hexval(*s);
        if (v < 0)
            return -1;
        if (!have_nibble) {
            cur = (uint8_t)(v << 4);
            have_nibble = 1;
        } else {
            if (len >= cap)
                return -1;
            buf[len++] = (uint8_t)(cur | (uint8_t)v);
            have_nibble = 0;
        }
    }
    if (have_nibble)
        return -1; /* odd number of digits */
    if (len == 0)
        return -1; /* empty pattern */

    *out_len = len;
    return 0;
}

int hv_pread_full(int fd, void *buf, size_t n, uint64_t off, size_t *got) {
    uint8_t *p = buf;
    size_t done = 0;
    while (done < n) {
        size_t chunk = n - done;
        /* Offsets above OFF_MAX are not representable; treat as EOF. */
        if (off > (uint64_t)INT64_MAX)
            break;
        ssize_t r = pread(fd, p + done, chunk, (off_t)off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            break; /* EOF */
        done += (size_t)r;
        off += (uint64_t)r;
    }
    *got = done;
    return 0;
}
