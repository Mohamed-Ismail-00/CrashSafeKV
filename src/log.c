/*
 * log.c - Log header handling and durable record appends.
 */
#include "log.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "record.h"

cskv_status log_write_header(FILE *fp)
{
    uint8_t h[LOG_HEADER_SIZE];
    memcpy(h, LOG_MAGIC, 4);
    rec_put_u16(h + 4, LOG_VERSION);
    rec_put_u16(h + 6, 0);                 /* reserved */
    return fwrite(h, 1, sizeof h, fp) == sizeof h ? CSKV_OK : CSKV_ERR_IO;
}

cskv_status log_check_header(FILE *fp)
{
    uint8_t h[LOG_HEADER_SIZE];
    rewind(fp);
    if (fread(h, 1, sizeof h, fp) != sizeof h)
        return CSKV_ERR_BAD_FORMAT;
    if (memcmp(h, LOG_MAGIC, 4) != 0 || rec_get_u16(h + 4) != LOG_VERSION)
        return CSKV_ERR_BAD_FORMAT;
    return CSKV_OK;
}

cskv_status log_append(FILE *fp, uint64_t cur_size, uint8_t type,
                       const void *key, size_t klen,
                       const void *val, size_t vlen,
                       int durable, size_t *written)
{
    /* Encode into one contiguous buffer so the record goes out in a single
     * fwrite - never a header without its body from our side. */
    size_t n = rec_encoded_size(klen, vlen);
    uint8_t *buf = malloc(n);
    if (buf == NULL)
        return CSKV_ERR_NO_MEMORY;
    rec_encode(buf, type, key, klen, val, vlen);

    /* fflush always: hands the bytes to the OS, so killing the process can
     * no longer lose them. plat_fsync (_commit) additionally survives power
     * loss, and is only done in durable (sync = always) mode. */
    int ok = fwrite(buf, 1, n, fp) == n &&
             fflush(fp) == 0 &&
             (!durable || plat_fsync(fp) == 0);
    free(buf);

    if (!ok) {
        clearerr(fp);
        /* Drop whatever part of the record reached the file. If even this
         * fails, recovery will detect and cut the partial record later. */
        (void)plat_truncate(fp, (int64_t)cur_size);
        return CSKV_ERR_IO;
    }
    *written = n;
    return CSKV_OK;
}
