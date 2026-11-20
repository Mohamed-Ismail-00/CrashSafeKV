/*
 * record.c - Encode / decode / validate single log records.
 * Validation order follows the numbered rules in CLAUDE.md section 8.4.
 */
#include "record.h"

#include <string.h>

#include "crc32.h"

/* ---- little-endian helpers ---------------------------------------------- */

void rec_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void rec_put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

void rec_put_u64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

uint16_t rec_get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t rec_get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

uint64_t rec_get_u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

/* ---- records ------------------------------------------------------------ */

const char *rec_result_str(rec_result r)
{
    switch (r) {
    case REC_OK:                return "ok";
    case REC_ERR_TRUNC_HEADER:  return "truncated header";
    case REC_ERR_BAD_LENGTH:    return "impossible length";
    case REC_ERR_TRUNC_BODY:    return "truncated body";
    case REC_ERR_CRC:           return "CRC mismatch";
    case REC_ERR_BAD_TYPE:      return "unknown record type";
    case REC_ERR_BAD_LENGTHS:   return "inconsistent lengths";
    case REC_ERR_BAD_TOMBSTONE: return "invalid tombstone";
    }
    return "unknown error";
}

size_t rec_encoded_size(size_t klen, size_t vlen)
{
    return REC_HEADER_SIZE + REC_BODY_FIXED + klen + vlen;
}

void rec_encode(uint8_t *buf, uint8_t type, const void *key, size_t klen,
                const void *val, size_t vlen)
{
    uint8_t *body = buf + REC_HEADER_SIZE;
    uint32_t body_len = (uint32_t)(REC_BODY_FIXED + klen + vlen);

    /* Body first, because the header's CRC is computed over it. */
    body[0] = type;
    rec_put_u32(body + 1, (uint32_t)klen);
    rec_put_u32(body + 5, (uint32_t)vlen);
    if (klen > 0)
        memcpy(body + REC_BODY_FIXED, key, klen);
    if (vlen > 0)
        memcpy(body + REC_BODY_FIXED + klen, val, vlen);

    rec_put_u32(buf, body_len);
    rec_put_u32(buf + 4, crc32_compute(body, body_len));
}

rec_result rec_parse_header(const uint8_t *hdr, uint64_t avail,
                            uint32_t *body_len, uint32_t *crc)
{
    if (avail < REC_HEADER_SIZE)
        return REC_ERR_TRUNC_HEADER;                       /* rule 1 */
    *body_len = rec_get_u32(hdr);
    *crc = rec_get_u32(hdr + 4);
    if (*body_len < REC_BODY_FIXED || *body_len > REC_MAX_BODY)
        return REC_ERR_BAD_LENGTH;                         /* rule 2 */
    if (avail - REC_HEADER_SIZE < *body_len)
        return REC_ERR_TRUNC_BODY;                         /* rule 3 */
    return REC_OK;
}

rec_result rec_parse_body(const uint8_t *body, uint32_t body_len,
                          uint32_t crc, rec_view *out)
{
    if (crc32_compute(body, body_len) != crc)
        return REC_ERR_CRC;                                /* rule 4 */

    uint8_t type = body[0];
    if (type != REC_SET && type != REC_DEL)
        return REC_ERR_BAD_TYPE;                           /* rule 5 */

    uint32_t klen = rec_get_u32(body + 1);
    uint32_t vlen = rec_get_u32(body + 5);
    /* 64-bit sum so huge klen/vlen values cannot wrap around. */
    uint64_t expected = (uint64_t)REC_BODY_FIXED + klen + vlen;
    if (klen == 0 || klen > CSKV_MAX_KEY_LEN || vlen > CSKV_MAX_VALUE_LEN ||
        expected != body_len)
        return REC_ERR_BAD_LENGTHS;                        /* rule 6 */

    if (type == REC_DEL && vlen != 0)
        return REC_ERR_BAD_TOMBSTONE;                      /* rule 7 */

    out->type = type;
    out->key = body + REC_BODY_FIXED;
    out->klen = klen;
    out->val = body + REC_BODY_FIXED + klen;
    out->vlen = vlen;
    return REC_OK;
}

rec_result rec_decode(const uint8_t *buf, size_t avail,
                      rec_view *out, size_t *consumed)
{
    uint32_t body_len, crc;
    rec_result r = rec_parse_header(buf, avail, &body_len, &crc);
    if (r != REC_OK)
        return r;
    r = rec_parse_body(buf + REC_HEADER_SIZE, body_len, crc, out);
    if (r == REC_OK)
        *consumed = REC_HEADER_SIZE + body_len;
    return r;
}
