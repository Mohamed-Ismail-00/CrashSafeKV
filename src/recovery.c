/*
 * recovery.c - Startup replay (CLAUDE.md 8.5 "Recovery").
 *
 *   offset = 8
 *   while bytes remain:
 *       read + validate the record at offset (rules in record.c)
 *       invalid -> truncate the file at offset, report why, stop
 *       valid   -> SET: put into table, DEL: remove from table
 *
 * Only whole, checksum-verified records ever reach the hash table.
 */
#include "recovery.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "platform.h"
#include "record.h"

/* One body buffer reused for every record; grows to the largest body seen. */
typedef struct {
    uint8_t *data;
    size_t cap;
} body_buf;

static int ensure_cap(body_buf *b, size_t need)
{
    if (need <= b->cap)
        return 0;
    uint8_t *p = realloc(b->data, need);
    if (p == NULL)
        return -1;
    b->data = p;
    b->cap = need;
    return 0;
}

/* Reads the record at the current position. The return value reports real
 * failures (I/O, memory); *res reports whether the record itself is valid. */
static cskv_status read_record(FILE *fp, uint64_t remaining, body_buf *b,
                               rec_view *view, uint32_t *body_len,
                               rec_result *res)
{
    uint8_t hdr[REC_HEADER_SIZE];
    uint32_t crc;
    size_t want = remaining < REC_HEADER_SIZE ? (size_t)remaining
                                              : REC_HEADER_SIZE;

    if (fread(hdr, 1, want, fp) != want)
        return CSKV_ERR_IO;
    *res = rec_parse_header(hdr, remaining, body_len, &crc);
    if (*res != REC_OK)
        return CSKV_OK;

    if (ensure_cap(b, *body_len) != 0)
        return CSKV_ERR_NO_MEMORY;
    if (fread(b->data, 1, *body_len, fp) != *body_len)
        return CSKV_ERR_IO;
    *res = rec_parse_body(b->data, *body_len, crc, view);
    return CSKV_OK;
}

static cskv_status apply_record(ht *table, const rec_view *v)
{
    if (v->type == REC_SET)
        return ht_put(table, v->key, v->klen, v->val, v->vlen) == 0
                   ? CSKV_OK : CSKV_ERR_NO_MEMORY;
    ht_remove(table, v->key, v->klen);     /* DEL of a missing key is fine */
    return CSKV_OK;
}

/* Record the corruption and cut the file back to the last valid record, so
 * new appends start from a clean point. */
static cskv_status cut_tail(FILE *fp, uint64_t offset, uint64_t file_size,
                            rec_result why, cskv_recovery_info *info)
{
    info->corruption_found = 1;
    info->bytes_discarded = file_size - offset;
    snprintf(info->reason, sizeof info->reason, "%s at offset %llu",
             rec_result_str(why), (unsigned long long)offset);
    if (plat_truncate(fp, (int64_t)offset) != 0 || plat_fsync(fp) != 0)
        return CSKV_ERR_IO;
    return CSKV_OK;
}

cskv_status recovery_replay(FILE *fp, ht *table, cskv_recovery_info *info)
{
    double start = plat_now_ms();
    memset(info, 0, sizeof *info);

    int64_t size = plat_file_size(fp);
    if (size < 0)
        return CSKV_ERR_IO;
    uint64_t file_size = (uint64_t)size;
    uint64_t offset = LOG_HEADER_SIZE;
    body_buf buf = {NULL, 0};
    cskv_status st = CSKV_OK;

    while (offset < file_size) {           /* offset == size: clean end */
        rec_view v;
        uint32_t body_len = 0;
        rec_result res;

        st = read_record(fp, file_size - offset, &buf, &v, &body_len, &res);
        if (st != CSKV_OK)
            break;
        if (res != REC_OK) {
            st = cut_tail(fp, offset, file_size, res, info);
            break;
        }
        st = apply_record(table, &v);
        if (st != CSKV_OK)
            break;
        info->records_replayed++;
        offset += REC_HEADER_SIZE + body_len;
    }

    free(buf.data);
    info->bytes_valid = offset;
    info->duration_ms = plat_now_ms() - start;
    return st;
}
