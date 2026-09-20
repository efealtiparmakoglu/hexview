/* hexdump.h — canonical hex dump formatting and the two dump engines. */
#ifndef HV_HEXDUMP_H
#define HV_HEXDUMP_H

#include <stdint.h>
#include <stdio.h>

#define HV_MAX_WIDTH 64

typedef struct {
    int color;      /* 1 = emit ANSI escapes */
    int collapse;   /* 1 = fold repeated lines with "*" */
    int width;      /* bytes per line, 1..HV_MAX_WIDTH */
} hv_dump_opts;

typedef enum { HV_COLOR_AUTO = 0, HV_COLOR_ALWAYS, HV_COLOR_NEVER } hv_color_mode;

/*
 * Formats one line into dst (cap bytes): offset, hex area padded to a full
 * line width, and the "|ascii|" column. Returns the number of bytes written,
 * or -1 if dst is too small. `n` is the number of valid bytes in `bytes`
 * (1..width).
 */
long hv_format_line(char *dst, size_t cap, const uint8_t *bytes, size_t n,
                    uint64_t offset, const hv_dump_opts *o);

/*
 * Dumps a seekable file (or a window of it) to out. start is the absolute
 * first offset; len_limit bounds the number of bytes (use UINT64_MAX for
 * "to EOF"). Returns 0 on success, -1 on I/O error.
 */
int hv_dump_fd(FILE *out, int fd, uint64_t start, uint64_t len_limit,
               const hv_dump_opts *o);

/*
 * Dumps a non-seekable stream (stdin) to out, labeling the first byte with
 * start. Returns 0 on success, -1 on I/O error.
 */
int hv_dump_stream(FILE *out, FILE *in, uint64_t start, const hv_dump_opts *o);

#endif
