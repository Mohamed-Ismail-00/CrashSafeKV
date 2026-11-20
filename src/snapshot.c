/*
 * snapshot.c - Save and load snapshot files.
 *
 * Save streams entries straight from the hash table and keeps a running
 * CRC32 of every byte written. Load reads the whole file into memory and
 * checks magic, version, CRC and every length BEFORE building a table, so a
 * bad snapshot can never leak into the live state.
 */
#include "snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "platform.h"
#include "record.h"

#define SNAP_MAGIC       "CSNP"
#define SNAP_VERSION     1u
#define SNAP_HEADER_SIZE 16u     /* magic + version + reserved + entry_count */
#define SNAP_CRC_SIZE    4u
#define SNAP_PATH_MAX    280

/* ---- save --------------------------------------------------------------- */

typedef struct {
    FILE *fp;
    uint32_t crc;        /* running CRC of everything written */
    int failed;
} snap_writer;

static void put_bytes(snap_writer *w, const void *data, size_t n)
{
    if (w->failed || n == 0)
        return;
    if (fwrite(data, 1, n, w->fp) != n)
        w->failed = 1;
    w->crc = crc32_update(w->crc, data, n);
}

static int write_entry(const void *key, size_t klen, const void *val,
                       size_t vlen, void *user)
{
    snap_writer *w = user;
    uint8_t lens[8];
    rec_put_u32(lens, (uint32_t)klen);
    rec_put_u32(lens + 4, (uint32_t)vlen);
    put_bytes(w, lens, sizeof lens);
    put_bytes(w, key, klen);
    put_bytes(w, val, vlen);
    return w->failed;
}

/* Writes the complete snapshot to an already opened file. */
static int write_snapshot(FILE *fp, const ht *table)
{
    snap_writer w = {fp, 0, 0};
    uint8_t hdr[SNAP_HEADER_SIZE];
    memcpy(hdr, SNAP_MAGIC, 4);
    rec_put_u16(hdr + 4, SNAP_VERSION);
    rec_put_u16(hdr + 6, 0);
    rec_put_u64(hdr + 8, ht_count(table));
    put_bytes(&w, hdr, sizeof hdr);

    ht_foreach(table, write_entry, &w);

    uint8_t crc[SNAP_CRC_SIZE];
    rec_put_u32(crc, w.crc);          /* CRC covers everything before it */
    if (!w.failed && fwrite(crc, 1, sizeof crc, fp) != sizeof crc)
        w.failed = 1;
    return w.failed ? -1 : 0;
}

cskv_status snapshot_save(const ht *table, const char *path)
{
    char tmp[SNAP_PATH_MAX];
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n <= 0 || n >= (int)sizeof tmp)
        return CSKV_ERR_INVALID_ARG;

    FILE *fp = fopen(tmp, "wb");
    if (fp == NULL)
        return CSKV_ERR_IO;
    int failed = write_snapshot(fp, table) != 0 || plat_fsync(fp) != 0;
    failed |= fclose(fp) != 0;

    if (failed || plat_atomic_replace(tmp, path) != 0) {
        plat_remove(tmp);
        return CSKV_ERR_IO;
    }
    return CSKV_OK;
}

/* ---- load --------------------------------------------------------------- */

/* Reads the whole file into a malloc'd buffer. */
static cskv_status read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return CSKV_ERR_IO;
    int64_t size = plat_file_size(fp);
    if (size < 0) {
        fclose(fp);
        return CSKV_ERR_IO;
    }
    uint8_t *buf = malloc(size > 0 ? (size_t)size : 1);
    if (buf == NULL) {
        fclose(fp);
        return CSKV_ERR_NO_MEMORY;
    }
    size_t got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (got != (size_t)size) {
        free(buf);
        return CSKV_ERR_IO;
    }
    *out = buf;
    *out_len = (size_t)size;
    return CSKV_OK;
}

/* Header and whole-file CRC. */
static int check_envelope(const uint8_t *buf, size_t len)
{
    if (len < SNAP_HEADER_SIZE + SNAP_CRC_SIZE)
        return -1;
    if (memcmp(buf, SNAP_MAGIC, 4) != 0 || rec_get_u16(buf + 4) != SNAP_VERSION)
        return -1;
    size_t body = len - SNAP_CRC_SIZE;
    return crc32_compute(buf, body) == rec_get_u32(buf + body) ? 0 : -1;
}

/* Parses exactly entry_count entries into `table`. Every length is checked
 * against the bytes that remain, and the entries must end exactly at the
 * CRC field. */
static cskv_status parse_entries(const uint8_t *buf, size_t len, ht *table)
{
    uint64_t count = rec_get_u64(buf + 8);
    size_t pos = SNAP_HEADER_SIZE;
    size_t end = len - SNAP_CRC_SIZE;

    for (uint64_t i = 0; i < count; i++) {
        if (end - pos < 8)
            return CSKV_ERR_CORRUPT;
        uint32_t klen = rec_get_u32(buf + pos);
        uint32_t vlen = rec_get_u32(buf + pos + 4);
        pos += 8;
        if (klen == 0 || klen > CSKV_MAX_KEY_LEN ||
            vlen > CSKV_MAX_VALUE_LEN || end - pos < (size_t)klen + vlen)
            return CSKV_ERR_CORRUPT;
        if (ht_put(table, buf + pos, klen, buf + pos + klen, vlen) != 0)
            return CSKV_ERR_NO_MEMORY;
        pos += (size_t)klen + vlen;
    }
    return pos == end ? CSKV_OK : CSKV_ERR_CORRUPT;
}

cskv_status snapshot_load(const char *path, ht **out)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    if (path == NULL || out == NULL)
        return CSKV_ERR_INVALID_ARG;

    cskv_status st = read_file(path, &buf, &len);
    if (st != CSKV_OK)
        return st;

    ht *table = NULL;
    if (check_envelope(buf, len) != 0)
        st = CSKV_ERR_CORRUPT;
    else if ((table = ht_create()) == NULL)
        st = CSKV_ERR_NO_MEMORY;
    else
        st = parse_entries(buf, len, table);
    free(buf);

    if (st != CSKV_OK) {
        ht_destroy(table);
        return st;
    }
    *out = table;
    return CSKV_OK;
}
