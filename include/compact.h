/*
 * compact.h - Write a fresh log that holds only the given live keys.
 *
 * Used by COMPACT (table = current state) and by RESTORE (table = state
 * loaded from a snapshot). The caller then atomically renames the result
 * over cskv.log; this module never touches cskv.log itself.
 */
#ifndef CSKV_COMPACT_H
#define CSKV_COMPACT_H

#include <stdint.h>

#include "cskv.h"
#include "hashtable.h"

/* Writes header + one SET record per entry of `table` to `tmp_path`, then
 * flushes, commits and closes it. On success *out_bytes = file size.
 * On failure the tmp file is deleted and an error is returned. */
cskv_status compact_write(const ht *table, const char *tmp_path,
                          uint64_t *out_bytes);

#endif /* CSKV_COMPACT_H */
