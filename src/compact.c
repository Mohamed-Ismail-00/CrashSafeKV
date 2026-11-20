/*
 * compact.c - Build a compacted log in a temporary file (CLAUDE.md 8.5).
 *
 * Old log:  SET a=1, SET b=2, SET a=3, DEL b      (4 records)
 * New log:  SET a=3                               (1 record)
 *
 * Only the hash table is read, so the original log is never modified here.
 */
#include "compact.h"

#include <stdio.h>

#include "log.h"
#include "platform.h"
#include "record.h"

typedef struct {
    FILE *fp;
    uint64_t size;       /* bytes written so far */
    cskv_status st;
} compact_ctx;

static int write_entry(const void *key, size_t klen, const void *val,
                       size_t vlen, void *user)
{
    compact_ctx *ctx = user;
    size_t written = 0;
    /* durable = 0: one commit for the whole file at the end is enough,
     * because the file is only used after the final rename. */
    ctx->st = log_append(ctx->fp, ctx->size, REC_SET, key, klen, val, vlen,
                         0, &written);
    if (ctx->st != CSKV_OK)
        return 1;                    /* stop iterating */
    ctx->size += written;
    return 0;
}

cskv_status compact_write(const ht *table, const char *tmp_path,
                          uint64_t *out_bytes)
{
    FILE *fp = fopen(tmp_path, "wb");
    if (fp == NULL)
        return CSKV_ERR_IO;

    compact_ctx ctx = {fp, LOG_HEADER_SIZE, CSKV_OK};
    ctx.st = log_write_header(fp);
    if (ctx.st == CSKV_OK)
        ht_foreach(table, write_entry, &ctx);
    if (ctx.st == CSKV_OK && plat_fsync(fp) != 0)
        ctx.st = CSKV_ERR_IO;
    if (fclose(fp) != 0 && ctx.st == CSKV_OK)
        ctx.st = CSKV_ERR_IO;

    if (ctx.st != CSKV_OK) {
        plat_remove(tmp_path);
        return ctx.st;
    }
    *out_bytes = ctx.size;
    return CSKV_OK;
}
