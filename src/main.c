/* main.c — hexview: fast, colorful, less-style hex viewer. Zero dependencies
 * beyond libc. See README.md.
 *
 * Usage: hexview [options] <file|->
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hexdump.h"
#include "pager.h"
#include "util.h"

#define VERSION "1.0.0"

static const char USAGE[] =
    "hexview " VERSION " - fast, colorful, less-style hex viewer\n"
    "\n"
    "USAGE:\n"
    "    hexview [options] <file|->\n"
    "\n"
    "OPTIONS:\n"
    "    -c, --cols <N>      bytes per line, 1..64 (default 16)\n"
    "    -s, --start <OFF>   start at offset OFF (decimal, 0x hex, K/M/G suffix)\n"
    "    -l, --length <N>    show at most N bytes\n"
    "    -i, --interactive   force the full-screen pager\n"
    "        --dump          force plain dump mode (no pager)\n"
    "        --color MODE    auto | always | never (default: auto)\n"
    "        --no-collapse   do not fold repeated lines with '*'\n"
    "    -h, --help          show this help\n"
    "    -V, --version       show version\n"
    "\n"
    "With no -i/--dump, hexview uses the interactive pager when both stdin\n"
    "and stdout are a terminal and the input is a regular file; otherwise it\n"
    "dumps to stdout. See the README for pager keys.\n";

static void die_usage(const char *msg) {
    fprintf(stderr, "hexview: %s (try --help)\n", msg);
    exit(2);
}

static void print_version(void) {
    puts("hexview " VERSION);
    exit(0);
}

static void print_help(void) {
    fputs(USAGE, stdout);
    exit(0);
}

static int color_from_mode(hv_color_mode mode) {
    switch (mode) {
    case HV_COLOR_ALWAYS:
        return 1;
    case HV_COLOR_NEVER:
        return 0;
    default:
        return isatty(STDOUT_FILENO);
    }
}

int main(int argc, char **argv) {
    hv_dump_opts opts = {0, 1, 16};
    hv_color_mode color_mode = HV_COLOR_AUTO;
    uint64_t start = 0;
    uint64_t length = UINT64_MAX;
    int force_interactive = 0;
    int force_dump = 0;
    const char *positional = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        /* Split long options of the form "--name=value". */
        char namebuf[32];
        const char *eq = a[0] == '-' && a[1] == '-' ? strchr(a, '=') : NULL;
        const char *inline_val = NULL;
        if (eq != NULL) {
            size_t nlen = (size_t)(eq - a);
            if (nlen == 0 || nlen >= sizeof namebuf)
                die_usage("bad option");
            memcpy(namebuf, a, nlen);
            namebuf[nlen] = '\0';
            inline_val = eq + 1;
            a = namebuf;
        }

#define NEED_VALUE(opt)                                                       \
    (inline_val != NULL ? inline_val : (++i < argc ? argv[i] : NULL))

        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0)
            print_help();
        else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0)
            print_version();
        else if (strcmp(a, "-i") == 0 || strcmp(a, "--interactive") == 0)
            force_interactive = 1;
        else if (strcmp(a, "--dump") == 0)
            force_dump = 1;
        else if (strcmp(a, "--no-collapse") == 0)
            opts.collapse = 0;
        else if (strcmp(a, "--color") == 0) {
            const char *val = NEED_VALUE(color);
            if (val == NULL)
                die_usage("--color needs a mode");
            if (strcmp(val, "auto") == 0)
                color_mode = HV_COLOR_AUTO;
            else if (strcmp(val, "always") == 0)
                color_mode = HV_COLOR_ALWAYS;
            else if (strcmp(val, "never") == 0)
                color_mode = HV_COLOR_NEVER;
            else
                die_usage("bad --color mode (auto|always|never)");
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "--cols") == 0) {
            const char *val = NEED_VALUE(cols);
            if (val == NULL)
                die_usage("-c needs a number");
            uint64_t w;
            if (hv_parse_u64(val, &w) != 0 || w < 1 || w > HV_MAX_WIDTH)
                die_usage("-c must be 1..64");
            opts.width = (int)w;
        } else if (strcmp(a, "-s") == 0 || strcmp(a, "--start") == 0) {
            const char *val = NEED_VALUE(start);
            if (val == NULL)
                die_usage("-s needs an offset");
            if (hv_parse_u64(val, &start) != 0)
                die_usage("bad -s offset");
        } else if (strcmp(a, "-l") == 0 || strcmp(a, "--length") == 0) {
            const char *val = NEED_VALUE(length);
            if (val == NULL)
                die_usage("-l needs a length");
            if (hv_parse_u64(val, &length) != 0)
                die_usage("bad -l length");
        } else if (a[0] == '-' && a[1] != '\0') {
            die_usage("unknown option");
        } else {
            if (positional != NULL)
                die_usage("exactly one input file expected");
            positional = argv[i];
        }
#undef NEED_VALUE
#undef HAVE_VALUE
    }

    if (positional == NULL) {
        fputs(USAGE, stderr);
        return 2;
    }
    if (force_interactive && force_dump)
        die_usage("--dump and -i are mutually exclusive");

    opts.color = color_from_mode(color_mode);

    if (strcmp(positional, "-") == 0) {
        if (force_interactive)
            die_usage("interactive mode needs a regular file");
        if (isatty(STDIN_FILENO)) {
            fprintf(stderr, "hexview: refusing to read the terminal (use 'hexview -' with a pipe)\n");
            return 1;
        }
        if (hv_dump_stream(stdout, stdin, start, &opts) != 0) {
            fprintf(stderr, "hexview: read error on stdin\n");
            return 1;
        }
        if (fflush(stdout) != 0 || ferror(stdout)) {
            fprintf(stderr, "hexview: write error\n");
            return 1;
        }
        return 0;
    }

    int fd = open(positional, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "hexview: %s: %s\n", positional, strerror(errno));
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        fprintf(stderr, "hexview: %s: %s\n", positional, strerror(errno));
        close(fd);
        return 1;
    }

    int is_regular = S_ISREG(st.st_mode);
    uint64_t fsize = is_regular ? (uint64_t)st.st_size : UINT64_MAX;

    if (is_regular && start > fsize) {
        /* Seeking past EOF: nothing to show, matching dd/xxd behavior. */
        close(fd);
        return 0;
    }

    int use_pager = force_interactive ||
                    (!force_dump && is_regular && isatty(STDOUT_FILENO) && isatty(STDIN_FILENO));
    if (use_pager) {
        int r = hv_pager_run(fd, positional, fsize, &opts);
        close(fd);
        return r == 0 ? 0 : 1;
    }

    if (!is_regular) {
        /* Pipes etc.: stream. (open() only yields regular files here unless
         * the platform fills st_mode differently; be safe.) */
        close(fd);
        die_usage("input is not a regular file; use '-' to read a stream");
    }

    uint64_t len = length;
    if (len != UINT64_MAX && fsize - start < len)
        len = fsize - start;
    if (hv_dump_fd(stdout, fd, start, len, &opts) != 0) {
        fprintf(stderr, "hexview: %s: read error\n", positional);
        close(fd);
        return 1;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fprintf(stderr, "hexview: write error\n");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
