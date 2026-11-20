/*
 * log.h - The log file: 8-byte header followed by records (CLAUDE.md 8.4).
 *
 *   magic "CSKV" (4) | version = 1 (2) | reserved = 0 (2)
 */
#ifndef CSKV_LOG_H
#define CSKV_LOG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cskv.h"

#define LOG_HEADER_SIZE 8u
#define LOG_MAGIC       "CSKV"
#define LOG_VERSION     1u

/* Write the 8-byte header at the current position. */
cskv_status log_write_header(FILE *fp);

/* Read the header from offset 0. CSKV_ERR_BAD_FORMAT if the magic/version is
 * wrong or the file is shorter than a header. On success the stream is left
 * at offset 8, the first record. */
cskv_status log_check_header(FILE *fp);

/* Encode one record and write it at the end of `fp`.
 *   cur_size  file size before this append (used to roll back a partial write)
 *   durable   non-zero: also force the data to disk (_commit) before returning
 * On success *written = bytes added. On failure the file is cut back to
 * cur_size (best effort) and CSKV_ERR_IO / CSKV_ERR_NO_MEMORY is returned. */
cskv_status log_append(FILE *fp, uint64_t cur_size, uint8_t type,
                       const void *key, size_t klen,
                       const void *val, size_t vlen,
                       int durable, size_t *written);

#endif /* CSKV_LOG_H */
