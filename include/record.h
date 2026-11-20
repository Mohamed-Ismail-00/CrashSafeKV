/*
 * record.h - Binary encoding of one log record (format: CLAUDE.md 8.4).
 *
 *   body_len (4) | crc32 (4) | type (1) | klen (4) | vlen (4) | key | value
 *   \_____ header (8) ____/ \___________ body (body_len bytes) ________/
 *
 * All integers are little-endian and written byte by byte, so the file
 * layout never depends on struct padding or CPU endianness.
 */
#ifndef CSKV_RECORD_H
#define CSKV_RECORD_H

#include <stddef.h>
#include <stdint.h>

#include "cskv.h"

#define REC_HEADER_SIZE  8u                       /* body_len + crc */
#define REC_BODY_FIXED   9u                       /* type + klen + vlen */
#define REC_MAX_BODY     (REC_BODY_FIXED + CSKV_MAX_KEY_LEN + CSKV_MAX_VALUE_LEN)

enum { REC_SET = 1, REC_DEL = 2 };

/* Why a record was rejected. One value per validation rule in 8.4. */
typedef enum {
    REC_OK = 0,
    REC_ERR_TRUNC_HEADER,    /* rule 1: fewer than 8 bytes left */
    REC_ERR_BAD_LENGTH,      /* rule 2: body_len out of range */
    REC_ERR_TRUNC_BODY,      /* rule 3: fewer than body_len bytes left */
    REC_ERR_CRC,             /* rule 4: checksum mismatch */
    REC_ERR_BAD_TYPE,        /* rule 5: type not SET/DEL */
    REC_ERR_BAD_LENGTHS,     /* rule 6: klen/vlen inconsistent */
    REC_ERR_BAD_TOMBSTONE    /* rule 7: DEL with a value */
} rec_result;

/* A decoded record. key/val point INTO the buffer that was decoded;
 * they are not copies and live as long as that buffer. */
typedef struct {
    uint8_t type;
    const uint8_t *key;
    uint32_t klen;
    const uint8_t *val;
    uint32_t vlen;
} rec_view;

/* Short description of a result, e.g. "CRC mismatch". */
const char *rec_result_str(rec_result r);

/* Bytes needed to encode a record: 8 + 9 + klen + vlen. */
size_t rec_encoded_size(size_t klen, size_t vlen);

/* Encode into `buf`, which must hold rec_encoded_size(klen, vlen) bytes.
 * The caller validates lengths first (key 1..MAX, value 0..MAX). */
void rec_encode(uint8_t *buf, uint8_t type, const void *key, size_t klen,
                const void *val, size_t vlen);

/* Rules 1-3: parse the 8-byte header. `avail` = bytes left in the file
 * counting from the start of this header. */
rec_result rec_parse_header(const uint8_t *hdr, uint64_t avail,
                            uint32_t *body_len, uint32_t *crc);

/* Rules 4-7: validate a body already read into memory. */
rec_result rec_parse_body(const uint8_t *body, uint32_t body_len,
                          uint32_t crc, rec_view *out);

/* Header + body from one memory buffer. On success *consumed = 8 + body_len. */
rec_result rec_decode(const uint8_t *buf, size_t avail,
                      rec_view *out, size_t *consumed);

/* Little-endian helpers, shared with the log and snapshot code. */
void     rec_put_u16(uint8_t *p, uint16_t v);
void     rec_put_u32(uint8_t *p, uint32_t v);
void     rec_put_u64(uint8_t *p, uint64_t v);
uint16_t rec_get_u16(const uint8_t *p);
uint32_t rec_get_u32(const uint8_t *p);
uint64_t rec_get_u64(const uint8_t *p);

#endif /* CSKV_RECORD_H */
