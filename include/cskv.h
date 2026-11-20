/*
 * cskv.h - Public API of the CrashSafeKV storage engine.
 *
 * Ownership rules:
 *   - The engine copies every key/value passed in; callers keep their buffers.
 *   - cskv_get() returns a malloc'd copy in *out_val; the caller must free() it.
 *   - cskv_close() releases everything owned by the handle.
 *
 * The engine never prints; errors are reported through cskv_status only.
 */
#ifndef CSKV_H
#define CSKV_H

#include <stddef.h>
#include <stdint.h>

#define CSKV_MAX_KEY_LEN    256u
#define CSKV_MAX_VALUE_LEN  (1024u * 1024u)

typedef enum {
    CSKV_OK = 0,
    CSKV_ERR_NOT_FOUND,
    CSKV_ERR_INVALID_ARG,     /* NULL ptr, empty key, too long, etc. */
    CSKV_ERR_NO_MEMORY,
    CSKV_ERR_IO,              /* fopen/fwrite/fread/commit failed */
    CSKV_ERR_CORRUPT,         /* bad header / bad snapshot checksum */
    CSKV_ERR_LOCKED,          /* another process holds the store */
    CSKV_ERR_BAD_FORMAT       /* wrong magic / unsupported version */
} cskv_status;

typedef enum { CSKV_SYNC_ALWAYS = 0, CSKV_SYNC_NONE = 1 } cskv_sync_mode;

typedef struct {
    uint64_t records_replayed;
    uint64_t bytes_valid;
    uint64_t bytes_discarded;     /* corrupted tail that was truncated */
    int      corruption_found;    /* 0/1 */
    char     reason[128];         /* e.g. "CRC mismatch at offset 4096" */
    double   duration_ms;
} cskv_recovery_info;

typedef struct {
    uint64_t live_keys;
    uint64_t log_bytes;
    uint64_t log_records;         /* total records in current log */
    uint64_t dead_records;        /* overwritten or deleted entries */
    cskv_recovery_info last_recovery;
} cskv_stats;

typedef struct cskv cskv;         /* opaque */

cskv_status cskv_open(const char *data_dir, cskv_sync_mode mode, cskv **out);
void        cskv_close(cskv *db);

cskv_status cskv_set(cskv *db, const void *key, size_t klen,
                     const void *val, size_t vlen);
/* *out_val is malloc'd by the engine; caller must free(). */
cskv_status cskv_get(cskv *db, const void *key, size_t klen,
                     void **out_val, size_t *out_vlen);
cskv_status cskv_del(cskv *db, const void *key, size_t klen);

/* Calls cb for each live key in sorted order. Stop early if cb returns non-zero. */
typedef int (*cskv_key_cb)(const void *key, size_t klen, void *user);
cskv_status cskv_keys(cskv *db, cskv_key_cb cb, void *user);

cskv_status cskv_compact(cskv *db);
cskv_status cskv_snapshot(cskv *db, const char *path);
cskv_status cskv_restore(cskv *db, const char *path);
cskv_status cskv_get_stats(cskv *db, cskv_stats *out);

const char *cskv_strerror(cskv_status s);

#endif /* CSKV_H */
