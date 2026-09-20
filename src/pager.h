/* pager.h — full-screen interactive viewer. */
#ifndef HV_PAGER_H
#define HV_PAGER_H

#include <stdint.h>

#include "hexdump.h"

/*
 * Runs the interactive pager on a seekable regular file. Requires stdin and
 * stdout to be a terminal. Returns 0 on clean exit, -1 on I/O failure or if
 * the terminal is not interactive.
 */
int hv_pager_run(int fd, const char *filename, uint64_t fsize, const hv_dump_opts *o);

#endif
