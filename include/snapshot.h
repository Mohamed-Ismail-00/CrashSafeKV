/*
 * snapshot.h - Full-state backup files (format: CLAUDE.md 8.5).
 *
 *   "CSNP" (4) | version = 1 (2) | reserved (2) | entry_count (8)
 *   entries: klen (4) | vlen (4) | key | value      (entry_count times)
 *   file_crc (4) = CRC32 of every byte before it
 */
#ifndef CSKV_SNAPSHOT_H
#define CSKV_SNAPSHOT_H

#include "cskv.h"
#include "hashtable.h"

/* Writes <path>.tmp, commits it, then atomically renames it to <path>.
 * An existing <path> is only replaced once the new file is complete. */
cskv_status snapshot_save(const ht *table, const char *path);

/* Reads and fully validates <path>, then builds a NEW table from it.
 * On success the caller owns *out (free with ht_destroy).
 * Errors: CSKV_ERR_IO (cannot read), CSKV_ERR_CORRUPT (any validation
 * failure), CSKV_ERR_NO_MEMORY. Nothing outside *out is ever modified. */
cskv_status snapshot_load(const char *path, ht **out);

#endif /* CSKV_SNAPSHOT_H */
