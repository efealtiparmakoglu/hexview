/* util.h — small shared helpers: parsing, byte classes, I/O. */
#ifndef HV_UTIL_H
#define HV_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Byte classification driving the color scheme. */
typedef enum {
    CL_ZERO = 0,   /* 0x00            -> dim    */
    CL_PRINT = 1,  /* 0x20..0x7E      -> green  */
    CL_OTHER = 2   /* everything else -> red    */
} byte_class;

byte_class hv_classify(uint8_t b);

/* ANSI escapes for each class plus offset and reset. */
extern const char *const HV_CLASS_COLOR[3];
#define HV_COLOR_OFFSET "\x1b[1;34m"
#define HV_COLOR_RESET  "\x1b[0m"

/*
 * Parses an offset/length: decimal, or 0x-prefixed hex, optional binary
 * suffix K/KiB, M/MiB, G/GiB (case-insensitive). Returns 0 on success,
 * -1 on malformed input or overflow.
 */
int hv_parse_u64(const char *s, uint64_t *out);

/*
 * Parses a hex byte pattern like "68 65 6c" or "68656c" into buf.
 * Returns 0 on success (*out_len set), -1 on malformed input or if the
 * pattern needs more than cap bytes.
 */
int hv_parse_hex(const char *s, uint8_t *buf, size_t cap, size_t *out_len);

/*
 * Reads up to n bytes at absolute offset off, retrying partial reads.
 * *got receives the number of bytes actually read (short only at EOF).
 * Returns 0 on success, -1 on I/O error (errno set).
 */
int hv_pread_full(int fd, void *buf, size_t n, uint64_t off, size_t *got);

#endif
