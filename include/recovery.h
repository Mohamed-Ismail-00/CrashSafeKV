/*
 * recovery.h - Replay the log on startup and cut off a corrupted tail.
 */
#ifndef CSKV_RECOVERY_H
#define CSKV_RECOVERY_H

#include <stdio.h>

#include "cskv.h"
#include "hashtable.h"

/* Replays every valid record of `fp` into `table`.
 *
 * Preconditions: fp is opened "r+b" and positioned right after a header
 * that log_check_header() accepted.
 *
 * At the first invalid record, the file is truncated to the end of the last
 * valid record and committed; details go into *info. Corruption is NOT an
 * error: the function still returns CSKV_OK. Errors are only real failures
 * (CSKV_ERR_IO, CSKV_ERR_NO_MEMORY). */
cskv_status recovery_replay(FILE *fp, ht *table, cskv_recovery_info *info);

#endif /* CSKV_RECOVERY_H */
