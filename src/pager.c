/* pager.c — full-screen interactive hex viewer.
 *
 * Pure termios + ANSI escapes (alternate screen, per-line repaint). Keys:
 *   j/k, arrows      line down/up          d/u, PgDn/PgUp, Space/b  half page
 *   g                goto offset (hex)     G                        end of file
 *   /                search hex pattern    n/N                      next/prev match
 *   r, Ctrl-L        redraw                ?, F1                    help
 *   q, Ctrl-C        quit
 */
#include "pager.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "util.h"

#define SEARCH_CAP 64

typedef struct {
    int fd;
    const char *filename;
    uint64_t fsize;
    hv_dump_opts opts;
    int width;    /* effective bytes per line (fits the terminal) */
    int rows;     /* viewport lines, excluding the status bar */
    uint64_t top; /* byte offset of the top viewport line (width-aligned) */
    uint8_t pattern[SEARCH_CAP];
    size_t pat_len;
    int pat_valid;
    char msg[128]; /* status-bar message area */
    struct termios saved;
    int raw_on;
} viewer;

/* ------------------------------------------------------------------ */
/* Terminal plumbing                                                   */

static int term_rows(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_row < 4)
        return 24;
    return ws.ws_row;
}

static int term_cols(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col < 20)
        return 80;
    return ws.ws_col;
}

static void raw_enable(viewer *v) {
    struct termios raw = v->saved;
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    v->raw_on = 1;
}

static void raw_disable(viewer *v) {
    if (v->raw_on) {
        tcsetattr(STDIN_FILENO, TCSANOW, &v->saved);
        v->raw_on = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Keys                                                                */

/* Reads one logical key: a byte, or a decoded special key >= 0x100. */
enum {
    K_UP = 0x100, K_DOWN, K_PGUP, K_PGDN, K_HOME, K_END, K_UNKNOWN
};

static int read_byte(void) {
    uint8_t c;
    ssize_t r;
    do {
        r = read(STDIN_FILENO, &c, 1);
    } while (r < 0 && errno == EINTR);
    return r == 1 ? c : -1;
}

static int read_key(void) {
    int c = read_byte();
    if (c < 0)
        return -1;
    if (c != 0x1b)
        return c;
    /* Escape sequence: try to disambiguate; a lone ESC is ignored. */
    int c2 = read_byte();
    if (c2 < 0)
        return K_UNKNOWN;
    if (c2 != '[' && c2 != 'O')
        return K_UNKNOWN;
    int c3 = read_byte();
    if (c3 < 0)
        return K_UNKNOWN;
    switch (c3) {
    case 'A':
        return K_UP;
    case 'B':
        return K_DOWN;
    case 'H':
        return K_HOME;
    case 'F':
        return K_END;
    case '5': {
        int c4 = read_byte();
        return c4 == '~' ? K_PGUP : K_UNKNOWN;
    }
    case '6': {
        int c4 = read_byte();
        return c4 == '~' ? K_PGDN : K_UNKNOWN;
    }
    default:
        return K_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* Viewport                                                            */

static void clamp_top(viewer *v) {
    if (v->fsize == 0) {
        v->top = 0;
        return;
    }
    uint64_t view = (uint64_t)v->rows * (uint64_t)v->width;
    uint64_t aligned_end = ((v->fsize + (uint64_t)v->width - 1) / (uint64_t)v->width) *
                           (uint64_t)v->width;
    uint64_t max_top = aligned_end > view ? aligned_end - view : 0;
    if (v->top > max_top)
        v->top = max_top;
}

/* Reads one viewport line at absolute line offset `off`. */
static int read_line(const viewer *v, uint64_t off, uint8_t *buf, size_t *got) {
    uint64_t remaining = off < v->fsize ? v->fsize - off : 0;
    size_t want = (uint64_t)v->width <= remaining ? (size_t)v->width : (size_t)remaining;
    if (want == 0) {
        *got = 0;
        return 0;
    }
    return hv_pread_full(v->fd, buf, want, off, got);
}

static void render(const viewer *v) {
    char line[8192];
    uint8_t bytes[HV_MAX_WIDTH];

    fputs("\x1b[H\x1b[?25l", stdout);
    for (int i = 0; i < v->rows; i++) {
        uint64_t off = v->top + (uint64_t)i * (uint64_t)v->width;
        size_t got = 0;
        if (read_line(v, off, bytes, &got) != 0 || got == 0) {
            fputs("\x1b[K\n", stdout);
            continue;
        }
        long len = hv_format_line(line, sizeof line, bytes, got, off, &v->opts);
        if (len < 0)
            len = 0;
        fwrite(line, 1, (size_t)len, stdout);
        fputs("\x1b[K", stdout);
    }

    /* Status bar, reverse video. */
    double pct = v->fsize > 0 ? 100.0 * (double)v->top / (double)v->fsize : 100.0;
    char bar[384];
    snprintf(bar, sizeof bar, " %s | %.1f KiB | @ 0x%llx (%.1f%%) | %s ",
             v->filename, (double)v->fsize / 1024.0,
             (unsigned long long)v->top, pct,
             v->msg[0] ? v->msg
                       : (v->pat_valid ? "n/N=search  ?=help" : "/=search  ?=help"));
    fputs("\x1b[7m", stdout);
    fputs(bar, stdout);
    fputs("\x1b[K\x1b[0m", stdout);
    fflush(stdout);
}

/* Bottom-line prompt. Returns 1 on Enter, 0 on cancel. */
static int prompt_line(const viewer *v, const char *label, char *out, size_t cap) {
    size_t len = 0;
    out[0] = '\0';
    fputs("\x1b[7m", stdout);
    printf(" %s", label);
    fflush(stdout);

    for (;;) {
        int c = read_byte();
        if (c < 0 || c == 0x1b) {
            fputs("\x1b[0m\x1b[K", stdout);
            fflush(stdout);
            return 0;
        }
        if (c == '\r' || c == '\n') {
            fputs("\x1b[0m\x1b[K", stdout);
            fflush(stdout);
            out[len] = '\0';
            return 1;
        }
        if ((c == 0x7f || c == '\b') && len > 0) {
            out[--len] = '\0';
        } else if (c >= 0x20 && c < 0x7f && len + 1 < cap) {
            out[len++] = (char)c;
            out[len] = '\0';
        } else {
            continue;
        }
        fputs("\x1b[7m", stdout);
        printf(" %s%s_", label, out);
        fputs("\x1b[0m\x1b[K", stdout);
        fflush(stdout);
    }
}

static void do_goto(viewer *v) {
    char input[32];
    if (!prompt_line(v, "goto offset (hex):", input, sizeof input))
        return;
    if (input[0] == '\0') {
        v->top = 0;
        clamp_top(v);
        return;
    }
    uint64_t target;
    if (hv_parse_u64(input, &target) != 0) {
        snprintf(v->msg, sizeof v->msg, "bad offset '%s'", input);
        return;
    }
    v->top = (target / (uint64_t)v->width) * (uint64_t)v->width;
    clamp_top(v);
}

/* ------------------------------------------------------------------ */
/* Search                                                              */

static int find_forward(viewer *v, uint64_t from, uint64_t *hit) {
    static uint8_t buf[1 << 16];
    uint64_t off = from;
    while (off < v->fsize) {
        size_t got;
        if (hv_pread_full(v->fd, buf, sizeof buf, off, &got) != 0)
            return -1;
        if (got == 0)
            break;
        if (got >= v->pat_len) {
            size_t scan = got - v->pat_len + 1;
            for (size_t i = 0; i < scan; i++) {
                if (buf[i] == v->pattern[0] &&
                    memcmp(buf + i, v->pattern, v->pat_len) == 0) {
                    *hit = off + i;
                    return 1;
                }
            }
        }
        if (got < sizeof buf)
            break;
        off += got - (v->pat_len - 1); /* overlap for boundary matches */
    }
    return 0;
}

static int find_backward(viewer *v, uint64_t before, uint64_t *hit) {
    static uint8_t buf[1 << 16];
    if (before == 0)
        return 0;
    uint64_t end = before; /* exclusive */
    while (end > 0) {
        uint64_t off = end > sizeof buf ? end - sizeof buf : 0;
        size_t got;
        if (hv_pread_full(v->fd, buf, (size_t)(end - off), off, &got) != 0)
            return -1;
        if (got == 0)
            break;
        /* Search this window backwards; extend the window by pat_len - 1 so
         * matches straddling the boundary are seen. */
        size_t scan = got >= v->pat_len ? got - v->pat_len + 1 : 0;
        for (size_t i = scan; i-- > 0;) {
            if (buf[i] == v->pattern[0] &&
                memcmp(buf + i, v->pattern, v->pat_len) == 0) {
                *hit = off + i;
                return 1;
            }
        }
        if (off == 0)
            break;
        end = off + (v->pat_len - 1);
    }
    return 0;
}

static void do_search_next(viewer *v) {
    if (!v->pat_valid) {
        snprintf(v->msg, sizeof v->msg, "no pattern (use /)");
        return;
    }
    uint64_t from = v->top + (uint64_t)v->width;
    uint64_t hit;
    int r = find_forward(v, from, &hit);
    if (r < 0)
        return;
    if (r == 0) {
        r = find_forward(v, 0, &hit); /* wrap */
        if (r <= 0) {
            snprintf(v->msg, sizeof v->msg, "not found");
            return;
        }
        snprintf(v->msg, sizeof v->msg, "search wrapped");
    }
    v->top = (hit / (uint64_t)v->width) * (uint64_t)v->width;
    clamp_top(v);
}

static void do_search_prev(viewer *v) {
    if (!v->pat_valid) {
        snprintf(v->msg, sizeof v->msg, "no pattern (use /)");
        return;
    }
    uint64_t hit;
    int r = find_backward(v, v->top, &hit);
    if (r < 0)
        return;
    if (r == 0) {
        /* wrap: search from EOF backwards */
        uint64_t from = v->fsize > v->pat_len ? v->fsize - v->pat_len : v->fsize;
        r = find_backward(v, from, &hit);
        if (r <= 0) {
            snprintf(v->msg, sizeof v->msg, "not found");
            return;
        }
        snprintf(v->msg, sizeof v->msg, "search wrapped");
    }
    v->top = (hit / (uint64_t)v->width) * (uint64_t)v->width;
    clamp_top(v);
}

static void do_search_prompt(viewer *v) {
    char input[SEARCH_CAP * 3 + 8];
    if (!prompt_line(v, "search hex (e.g. 68 65 6c):", input, sizeof input))
        return;
    size_t len;
    if (hv_parse_hex(input, v->pattern, SEARCH_CAP, &len) != 0) {
        snprintf(v->msg, sizeof v->msg, "bad hex pattern");
        return;
    }
    v->pat_len = len;
    v->pat_valid = 1;
    do_search_next(v);
}

/* ------------------------------------------------------------------ */
/* Help overlay                                                        */

static void render_help(viewer *v) {
    static const char *rows[] = {
        "hexview keys",
        "",
        "  j / Down        line down          k / Up          line up",
        "  d, PgDn, Space  half page down     u, PgUp, b      half page up",
        "  g               goto offset (hex)  G               end of file",
        "  /               search hex bytes   n / N           next / previous match",
        "  r, Ctrl-L       redraw             q, Ctrl-C       quit",
        "",
        "  any key closes this help",
    };
    fputs("\x1b[H\x1b[?25l\x1b[J", stdout);
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++)
        printf("%s\r\n", rows[i]);
    (void)v;
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Main loop                                                           */

int hv_pager_run(int fd, const char *filename, uint64_t fsize, const hv_dump_opts *o) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fputs("hexview: interactive mode needs a terminal (use --dump)\n", stderr);
        return -1;
    }

    viewer v;
    memset(&v, 0, sizeof v);
    v.fd = fd;
    v.filename = filename;
    v.fsize = fsize;
    v.opts = *o;
    v.opts.color = 1;
    v.opts.collapse = 0;
    v.width = o->width;

    if (tcgetattr(STDIN_FILENO, &v.saved) != 0)
        return -1;

    /* Fit the line into the terminal: 10 offset + (3w+1) hex + 2 + w + 2. */
    int cols = term_cols();
    int maxw = (cols - 15) / 4;
    if (maxw < 1)
        maxw = 1;
    if (v.width > maxw)
        v.width = maxw;
    if (v.width > HV_MAX_WIDTH)
        v.width = HV_MAX_WIDTH;

    fputs("\x1b[?1049h", stdout); /* alternate screen */
    raw_enable(&v);

    int quit = 0;
    int show_help = 0;
    while (!quit) {
        v.rows = term_rows() - 1; /* one line for the status bar */
        if (show_help) {
            render_help(&v);
        } else {
            clamp_top(&v);
            render(&v);
            v.msg[0] = '\0'; /* shown once in the status bar */
        }

        int key = read_key();
        switch (key) {
        case 'q':
        case 'Q':
        case 0x03: /* Ctrl-C */
            quit = 1;
            break;
        case '?':
            show_help = !show_help;
            break;
        case 'j':
        case K_DOWN:
            v.top += (uint64_t)v.width;
            clamp_top(&v);
            break;
        case 'k':
        case K_UP:
            if (v.top >= (uint64_t)v.width)
                v.top -= (uint64_t)v.width;
            break;
        case 'd':
        case 0x04: /* Ctrl-D */
        case K_PGDN:
        case ' ':
            v.top += (uint64_t)v.rows * (uint64_t)v.width / 2;
            clamp_top(&v);
            break;
        case 'u':
        case 0x15: /* Ctrl-U */
        case K_PGUP:
        case 'b': {
            uint64_t step = (uint64_t)v.rows * (uint64_t)v.width / 2;
            v.top = v.top > step ? v.top - step : 0;
            break;
        }
        case 'G':
            v.top = UINT64_MAX; /* clamp_top resolves to the last page */
            clamp_top(&v);
            break;
        case 'g':
            do_goto(&v);
            break;
        case '/':
            do_search_prompt(&v);
            break;
        case 'n':
            do_search_next(&v);
            break;
        case 'N':
            do_search_prev(&v);
            break;
        case 'r':
        case 0x0c: /* Ctrl-L */
        case K_UNKNOWN:
            break;
        case -1:
            quit = 1; /* EOF on stdin */
            break;
        default:
            break;
        }
    }

    raw_disable(&v);
    fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout);
    fflush(stdout);
    return 0;
}
