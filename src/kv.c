/*
 * kv.c - Engine facade: implements cskv.h on top of the log, recovery and
 * hash table modules.
 *
 * Durability order for every change (CLAUDE.md 13.6):
 *     write record -> flush -> commit -> update hash table -> return OK
 * so the in-memory state never contains something that is not in the log.
 */
#include "cskv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compact.h"
#include "hashtable.h"
#include "log.h"
#include "platform.h"
#include "record.h"
#include "recovery.h"
#include "snapshot.h"

#define CSKV_PATH_MAX 260   /* Windows MAX_PATH; ANSI file APIs are limited to it */

struct cskv {
    char log_path[CSKV_PATH_MAX];
    char tmp_path[CSKV_PATH_MAX];
    char lock_path[CSKV_PATH_MAX];
    void *lock;                  /* lock file handle (platform layer) */
    FILE *log;                   /* opened "ab": every write goes to the end */
    ht *table;                   /* live keys -> values */
    cskv_sync_mode sync;
    uint64_t log_bytes;          /* current size of cskv.log */
    uint64_t log_records;        /* records currently in cskv.log */
    cskv_recovery_info last_recovery;
};

/* ---- helpers ------------------------------------------------------------ */

static int join_path(char *out, const char *dir, const char *name)
{
    int n = snprintf(out, CSKV_PATH_MAX, "%s/%s", dir, name);
    return n > 0 && n < CSKV_PATH_MAX ? 0 : -1;
}

static cskv_status check_key(const void *key, size_t klen)
{
    if (key == NULL || klen == 0 || klen > CSKV_MAX_KEY_LEN)
        return CSKV_ERR_INVALID_ARG;
    return CSKV_OK;
}

/* Append one record and update the size counters on success. */
static cskv_status append(cskv *db, uint8_t type, const void *key, size_t klen,
                          const void *val, size_t vlen)
{
    size_t written = 0;
    if (db->log == NULL)         /* log could not be reopened after a swap */
        return CSKV_ERR_IO;
    cskv_status st = log_append(db->log, db->log_bytes, type, key, klen,
                                val, vlen, db->sync == CSKV_SYNC_ALWAYS,
                                &written);
    if (st == CSKV_OK) {
        db->log_bytes += written;
        db->log_records++;
    }
    return st;
}

/* ---- open / close ------------------------------------------------------- */

/* Given the log opened "r+b": write a header into an empty file, otherwise
 * validate the header and replay the records into the hash table. */
static cskv_status load_log(cskv *db, FILE *fp)
{
    int64_t size = plat_file_size(fp);
    if (size < 0)
        return CSKV_ERR_IO;

    if (size == 0) {   /* new store (or a crash before the header was written) */
        if (log_write_header(fp) != CSKV_OK || plat_fsync(fp) != 0)
            return CSKV_ERR_IO;
        memset(&db->last_recovery, 0, sizeof db->last_recovery);
        db->last_recovery.bytes_valid = LOG_HEADER_SIZE;
        return CSKV_OK;
    }

    cskv_status st = log_check_header(fp);
    if (st == CSKV_OK)
        st = recovery_replay(fp, db->table, &db->last_recovery);
    return st;
}

static cskv_status open_log(cskv *db)
{
    FILE *fp = fopen(db->log_path, "r+b");
    if (fp == NULL && !plat_file_exists(db->log_path))
        fp = fopen(db->log_path, "w+b");                 /* first run */
    if (fp == NULL)
        return CSKV_ERR_IO;

    cskv_status st = load_log(db, fp);
    if (fclose(fp) != 0 && st == CSKV_OK)
        st = CSKV_ERR_IO;
    if (st != CSKV_OK)
        return st;

    db->log_bytes = db->last_recovery.bytes_valid;
    db->log_records = db->last_recovery.records_replayed;
    db->log = fopen(db->log_path, "ab");
    return db->log != NULL ? CSKV_OK : CSKV_ERR_IO;
}

cskv_status cskv_open(const char *data_dir, cskv_sync_mode mode, cskv **out)
{
    if (data_dir == NULL || out == NULL ||
        (mode != CSKV_SYNC_ALWAYS && mode != CSKV_SYNC_NONE))
        return CSKV_ERR_INVALID_ARG;
    *out = NULL;

    cskv *db = calloc(1, sizeof *db);
    if (db == NULL)
        return CSKV_ERR_NO_MEMORY;
    db->sync = mode;

    cskv_status st = CSKV_OK;
    if (join_path(db->log_path, data_dir, "cskv.log") != 0 ||
        join_path(db->tmp_path, data_dir, "cskv.log.tmp") != 0 ||
        join_path(db->lock_path, data_dir, "cskv.lock") != 0)
        st = CSKV_ERR_INVALID_ARG;
    else if (plat_mkdir_p(data_dir) != 0)
        st = CSKV_ERR_IO;
    else if ((db->lock = plat_lock_acquire(db->lock_path)) == NULL)
        /* The lock file only exists while some process holds it (it is
         * delete-on-close), so its presence tells "locked" from "no access". */
        st = plat_file_exists(db->lock_path) ? CSKV_ERR_LOCKED : CSKV_ERR_IO;
    else if ((db->table = ht_create()) == NULL)
        st = CSKV_ERR_NO_MEMORY;

    /* A leftover .tmp means a compaction/restore crashed before its atomic
     * rename; cskv.log is still the valid copy, so just delete the .tmp. */
    if (st == CSKV_OK && plat_file_exists(db->tmp_path))
        plat_remove(db->tmp_path);
    if (st == CSKV_OK)
        st = open_log(db);

    if (st != CSKV_OK) {
        cskv_close(db);
        return st;
    }
    *out = db;
    return CSKV_OK;
}

void cskv_close(cskv *db)
{
    if (db == NULL)
        return;
    if (db->log != NULL) {
        plat_fsync(db->log);     /* clean shutdown: also commit sync=none data */
        fclose(db->log);
    }
    ht_destroy(db->table);
    plat_lock_release(db->lock); /* lock file is deleted on close */
    free(db);
}

/* ---- set / get / del ---------------------------------------------------- */

cskv_status cskv_set(cskv *db, const void *key, size_t klen,
                     const void *val, size_t vlen)
{
    if (db == NULL || check_key(key, klen) != CSKV_OK ||
        (val == NULL && vlen > 0) || vlen > CSKV_MAX_VALUE_LEN)
        return CSKV_ERR_INVALID_ARG;

    cskv_status st = append(db, REC_SET, key, klen, val, vlen);
    if (st != CSKV_OK)
        return st;               /* write failed: memory left untouched */
    /* The record is durable; if this put runs out of memory the value is
     * still recovered on the next start. */
    return ht_put(db->table, key, klen, val, vlen) == 0 ? CSKV_OK
                                                        : CSKV_ERR_NO_MEMORY;
}

cskv_status cskv_get(cskv *db, const void *key, size_t klen,
                     void **out_val, size_t *out_vlen)
{
    if (db == NULL || out_val == NULL || out_vlen == NULL ||
        check_key(key, klen) != CSKV_OK)
        return CSKV_ERR_INVALID_ARG;

    size_t vlen = 0;
    const void *v = ht_get(db->table, key, klen, &vlen);
    if (v == NULL)
        return CSKV_ERR_NOT_FOUND;

    void *copy = malloc(vlen ? vlen : 1);   /* never malloc(0) */
    if (copy == NULL)
        return CSKV_ERR_NO_MEMORY;
    memcpy(copy, v, vlen);
    *out_val = copy;
    *out_vlen = vlen;
    return CSKV_OK;
}

cskv_status cskv_del(cskv *db, const void *key, size_t klen)
{
    if (db == NULL || check_key(key, klen) != CSKV_OK)
        return CSKV_ERR_INVALID_ARG;
    if (ht_get(db->table, key, klen, NULL) == NULL)
        return CSKV_ERR_NOT_FOUND;           /* nothing is written */

    cskv_status st = append(db, REC_DEL, key, klen, NULL, 0);
    if (st == CSKV_OK)
        ht_remove(db->table, key, klen);
    return st;
}

/* ---- keys --------------------------------------------------------------- */

typedef struct {
    const void *key;
    size_t klen;
} key_ref;

typedef struct {
    key_ref *items;
    size_t n;
} key_list;

static int collect_key(const void *key, size_t klen, const void *val,
                       size_t vlen, void *user)
{
    (void)val;
    (void)vlen;
    key_list *list = user;
    list->items[list->n].key = key;
    list->items[list->n].klen = klen;
    list->n++;
    return 0;
}

/* Byte order, then shorter first: "a" < "ab" < "b". */
static int compare_keys(const void *a, const void *b)
{
    const key_ref *x = a, *y = b;
    size_t n = x->klen < y->klen ? x->klen : y->klen;
    int c = memcmp(x->key, y->key, n);
    if (c != 0)
        return c;
    return (x->klen > y->klen) - (x->klen < y->klen);
}

cskv_status cskv_keys(cskv *db, cskv_key_cb cb, void *user)
{
    if (db == NULL || cb == NULL)
        return CSKV_ERR_INVALID_ARG;
    size_t count = ht_count(db->table);
    if (count == 0)
        return CSKV_OK;

    key_list list = {malloc(count * sizeof(key_ref)), 0};
    if (list.items == NULL)
        return CSKV_ERR_NO_MEMORY;
    ht_foreach(db->table, collect_key, &list);
    qsort(list.items, list.n, sizeof(key_ref), compare_keys);

    for (size_t i = 0; i < list.n; i++)
        if (cb(list.items[i].key, list.items[i].klen, user) != 0)
            break;
    free(list.items);
    return CSKV_OK;
}

/* ---- compact / snapshot / restore --------------------------------------- */

/* Replace cskv.log with a fresh log holding exactly the entries of `table`:
 *   1. write cskv.log.tmp completely and commit it
 *   2. close the current log
 *   3. atomic rename tmp -> cskv.log
 *   4. reopen cskv.log for appending
 * A crash at any point leaves either the old or the new log, never a mix.
 * If step 1 or 3 fails, the original log is reopened unchanged. */
static cskv_status replace_log(cskv *db, const ht *table)
{
    uint64_t new_bytes = 0;
    cskv_status st = compact_write(table, db->tmp_path, &new_bytes);
    if (st != CSKV_OK)
        return st;               /* tmp already removed; old log still open */

    fclose(db->log);
    db->log = NULL;
    if (plat_atomic_replace(db->tmp_path, db->log_path) != 0) {
        plat_remove(db->tmp_path);
        st = CSKV_ERR_IO;
    }

    db->log = fopen(db->log_path, "ab");   /* old or new, whichever is there */
    if (db->log == NULL)
        return CSKV_ERR_IO;
    if (st == CSKV_OK) {
        db->log_bytes = new_bytes;
        db->log_records = ht_count(table);
    }
    return st;
}

cskv_status cskv_compact(cskv *db)
{
    if (db == NULL)
        return CSKV_ERR_INVALID_ARG;
    if (db->log == NULL)
        return CSKV_ERR_IO;
    return replace_log(db, db->table);
}

cskv_status cskv_snapshot(cskv *db, const char *path)
{
    if (db == NULL || path == NULL || path[0] == '\0')
        return CSKV_ERR_INVALID_ARG;
    return snapshot_save(db->table, path);
}

cskv_status cskv_restore(cskv *db, const char *path)
{
    if (db == NULL || path == NULL || path[0] == '\0')
        return CSKV_ERR_INVALID_ARG;
    if (db->log == NULL)
        return CSKV_ERR_IO;

    /* 1. Validate the whole snapshot into a separate table first. */
    ht *loaded = NULL;
    cskv_status st = snapshot_load(path, &loaded);
    if (st != CSKV_OK)
        return st;               /* current state untouched */

    /* 2. Make it durable as the new log, 3. then swap the tables. */
    st = replace_log(db, loaded);
    if (st != CSKV_OK) {
        ht_destroy(loaded);
        return st;
    }
    ht_destroy(db->table);
    db->table = loaded;
    return CSKV_OK;
}

/* ---- stats -------------------------------------------------------------- */

cskv_status cskv_get_stats(cskv *db, cskv_stats *out)
{
    if (db == NULL || out == NULL)
        return CSKV_ERR_INVALID_ARG;
    out->live_keys = ht_count(db->table);
    out->log_bytes = db->log_bytes;
    out->log_records = db->log_records;
    /* Every record that is not the current value of a live key is dead:
     * overwritten SETs, deleted SETs and the DEL tombstones themselves. */
    out->dead_records = db->log_records - out->live_keys;
    out->last_recovery = db->last_recovery;
    return CSKV_OK;
}
