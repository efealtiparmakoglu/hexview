/* tests.c — unit tests for parsers, the line formatter and the dump engine.
 * Built by `make unit`, binary `./unit`. TAP-flavored output; nonzero exit on
 * any failure.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hexdump.h"
#include "util.h"

static int failed = 0;
static int checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            fprintf(stderr, "not ok %d - %s:%d: %s\n", checks, __FILE__,   \
                    __LINE__, #cond);                                      \
            failed++;                                                      \
        }                                                                  \
    } while (0)

static void test_parse_u64(void) {
    uint64_t v;
    CHECK(hv_parse_u64("0", &v) == 0 && v == 0);
    CHECK(hv_parse_u64("123", &v) == 0 && v == 123);
    CHECK(hv_parse_u64("0x1f", &v) == 0 && v == 31);
    CHECK(hv_parse_u64("0xFF", &v) == 0 && v == 255);
    CHECK(hv_parse_u64("4K", &v) == 0 && v == 4096);
    CHECK(hv_parse_u64("4k", &v) == 0 && v == 4096);
    CHECK(hv_parse_u64("2MiB", &v) == 0 && v == 2097152);
    CHECK(hv_parse_u64("1G", &v) == 0 && v == 1073741824ull);
    CHECK(hv_parse_u64(" 0x10 ", &v) == 0 && v == 16);
    CHECK(hv_parse_u64("18446744073709551615", &v) == 0 && v == UINT64_MAX);
    /* rejects */
    CHECK(hv_parse_u64("", &v) != 0);
    CHECK(hv_parse_u64("abc", &v) != 0);
    CHECK(hv_parse_u64("12z", &v) != 0);
    CHECK(hv_parse_u64("0x", &v) != 0);
    CHECK(hv_parse_u64("18446744073709551616", &v) != 0); /* overflow */
    CHECK(hv_parse_u64("1T", &v) != 0);                   /* unsupported suffix */
}

static void test_parse_hex(void) {
    uint8_t buf[8];
    size_t n;
    CHECK(hv_parse_hex("68 65 6c", buf, sizeof buf, &n) == 0 && n == 3 &&
          buf[0] == 0x68 && buf[1] == 0x65 && buf[2] == 0x6c);
    CHECK(hv_parse_hex("68656C", buf, sizeof buf, &n) == 0 && n == 3 &&
          buf[2] == 0x6c);
    CHECK(hv_parse_hex("00", buf, sizeof buf, &n) == 0 && n == 1 && buf[0] == 0);
    CHECK(hv_parse_hex("6 8", buf, sizeof buf, &n) == 0 && n == 1 &&
          buf[0] == 0x68); /* spaces between nibbles are tolerated */
    /* rejects */
    CHECK(hv_parse_hex("", buf, sizeof buf, &n) != 0);
    CHECK(hv_parse_hex("686", buf, sizeof buf, &n) != 0);   /* odd */
    CHECK(hv_parse_hex("zz", buf, sizeof buf, &n) != 0);    /* not hex */
    uint8_t tiny[1];
    CHECK(hv_parse_hex("68656c", tiny, sizeof tiny, &n) != 0); /* cap */
}

static void test_classify(void) {
    CHECK(hv_classify(0x00) == CL_ZERO);
    CHECK(hv_classify(0x41) == CL_PRINT);
    CHECK(hv_classify(0x7e) == CL_PRINT);
    CHECK(hv_classify(0x1f) == CL_OTHER);
    CHECK(hv_classify(0x7f) == CL_OTHER);
    CHECK(hv_classify(0xff) == CL_OTHER);
}

static void test_format_line(void) {
    hv_dump_opts o = {0, 1, 4};
    char buf[512];
    long len;

    /* width 4, two bytes: hex area is "41 42" + 7 pad spaces, plus the
     * two spaces before the ASCII column. */
    uint8_t two[] = {0x41, 0x42};
    len = hv_format_line(buf, sizeof buf, two, 2, 0, &o);
    CHECK(len > 0);
    char want[128];
    snprintf(want, sizeof want, "00000000  41 42%*s|AB|\n", 9, "");
    CHECK(strcmp(buf, want) == 0);

    /* width 4, full line, higher offset; note the double gap after the
     * first group (i == w/2) and the mapped ASCII column. */
    uint8_t four[] = {0x00, 0x41, 0xff, 0x20};
    len = hv_format_line(buf, sizeof buf, four, 4, 0xfffffff0, &o);
    CHECK(len > 0);
    CHECK(strcmp(buf, "fffffff0  00 41  ff 20  |.A. |\n") == 0);

    /* width 1 */
    o.width = 1;
    uint8_t one[] = {0x7f};
    len = hv_format_line(buf, sizeof buf, one, 1, 5, &o);
    CHECK(len > 0);
    CHECK(strcmp(buf, "00000005  7f  |.|\n") == 0);

    /* color mode contains escapes */
    o.width = 4;
    o.color = 1;
    len = hv_format_line(buf, sizeof buf, four, 4, 0, &o);
    CHECK(len > 0 && strncmp(buf, "\x1b[1;34m", 7) == 0);
    CHECK(strstr(buf, "\x1b[32mA") != NULL && strstr(buf, "\x1b[32m ") != NULL);
    CHECK(strstr(buf, "\x1b[2m.") != NULL);

    /* overflow is refused, not a crash */
    o.color = 1;
    char tiny[16];
    CHECK(hv_format_line(tiny, sizeof tiny, four, 4, 0, &o) == -1);
}

static void write_temp(char *path, const uint8_t *data, size_t n) {
    int fd = mkstemp(path);
    if (fd < 0) {
        perror("mkstemp");
        exit(1);
    }
    if (write(fd, data, n) != (ssize_t)n) {
        perror("write");
        exit(1);
    }
    close(fd);
}

static void test_dump_fd_collapse(void) {
    /* 3 identical full lines (width 4), then different content. */
    uint8_t data[] = {1, 2, 3, 4, 1, 2, 3, 4, 1, 2, 3, 4, 9, 9, 9, 9, 0x41};
    char path[] = "/tmp/hexview-test-XXXXXX";
    write_temp(path, data, sizeof data);

    int fd = open(path, 0);
    CHECK(fd >= 0);
    char *out = NULL;
    size_t out_len = 0;
    FILE *ms = open_memstream(&out, &out_len);

    hv_dump_opts o = {0, 1, 4}; /* collapse on */
    CHECK(hv_dump_fd(ms, fd, 0, UINT64_MAX, &o) == 0);
    fclose(ms);
    close(fd);
    unlink(path);

    /* line 0 printed, line 1 folded to "*", line 2 folded silently,
     * line 3 printed at offset 0xc, final partial line at 0x10. */
    char want[256];
    char tail[128];
    snprintf(want, sizeof want,
             "00000000  01 02  03 04  |....|\n"
             "*\n"
             "0000000c  09 09  09 09  |....|\n");
    snprintf(tail, sizeof tail, "00000010  41%*s|A|\n", 12, "");
    strcat(want, tail);
    CHECK(out != NULL && strcmp(out, want) == 0);
    free(out);
}

static void test_dump_fd_no_collapse(void) {
    uint8_t data[] = {1, 2, 3, 4, 1, 2, 3, 4};
    char path[] = "/tmp/hexview-test-XXXXXX";
    write_temp(path, data, sizeof data);
    int fd = open(path, 0);
    char *out = NULL;
    size_t out_len = 0;
    FILE *ms = open_memstream(&out, &out_len);

    hv_dump_opts o = {0, 0, 4}; /* collapse off */
    CHECK(hv_dump_fd(ms, fd, 0, UINT64_MAX, &o) == 0);
    fclose(ms);
    close(fd);
    unlink(path);
    CHECK(out != NULL && strstr(out, "*\n") == NULL);
    CHECK(out != NULL && strstr(out, "00000004  01 02  03 04") != NULL);
    free(out);
}

static void test_dump_fd_window(void) {
    uint8_t data[12];
    for (int i = 0; i < 12; i++)
        data[i] = (uint8_t)i;
    char path[] = "/tmp/hexview-test-XXXXXX";
    write_temp(path, data, sizeof data);
    int fd = open(path, 0);
    char *out = NULL;
    size_t out_len = 0;
    FILE *ms = open_memstream(&out, &out_len);

    hv_dump_opts o = {0, 1, 4};
    CHECK(hv_dump_fd(ms, fd, 4, 5, &o) == 0); /* start=4, length=5 */
    fclose(ms);
    close(fd);
    unlink(path);

    static const char want[] =
        "00000004  04 05  06 07  |....|\n";
    char want2[128];
    snprintf(want2, sizeof want2, "00000008  08%*s|.|\n", 12, "");
    CHECK(out != NULL && strncmp(out, want, strlen(want)) == 0);
    CHECK(out != NULL && strcmp(out + strlen(want), want2) == 0);
    free(out);
}

static void test_dump_stream_partial(void) {
    /* Streamed input where the final line is partial. */
    hv_dump_opts o = {0, 0, 16};
    char *out = NULL;
    size_t out_len = 0;
    FILE *ms = open_memstream(&out, &out_len);
    FILE *in = fmemopen((void *)"hello", 5, "r");
    CHECK(in != NULL);
    CHECK(hv_dump_stream(ms, in, 0, &o) == 0);
    fclose(in);
    fclose(ms);
    /* 16-wide line, 5 bytes: "68 65 6c 6c 6f" + 34 pad spaces + the two
     * spaces before the ASCII column. */
    char want[128];
    snprintf(want, sizeof want, "00000000  68 65 6c 6c 6f%*s|hello|\n", 36, "");
    CHECK(out != NULL && strcmp(out, want) == 0);
    free(out);
}

static void test_dump_stream_carry(void) {
    /* A line split across fread boundaries must not corrupt output. */
    hv_dump_opts o = {0, 0, 4};
    char *out = NULL;
    size_t out_len = 0;
    FILE *ms = open_memstream(&out, &out_len);
    FILE *in = fmemopen((void *)"abcdefgh", 8, "r");
    CHECK(in != NULL);
    CHECK(hv_dump_stream(ms, in, 0, &o) == 0);
    fclose(in);
    fclose(ms);
    static const char want[] =
        "00000000  61 62  63 64  |abcd|\n"
        "00000004  65 66  67 68  |efgh|\n";
    CHECK(out != NULL && strcmp(out, want) == 0);
    free(out);
}

int main(void) {
    test_parse_u64();
    test_parse_hex();
    test_classify();
    test_format_line();
    test_dump_fd_collapse();
    test_dump_fd_no_collapse();
    test_dump_fd_window();
    test_dump_stream_partial();
    test_dump_stream_carry();
    printf("%d checks, %d failures\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
