/* test_record.c - Round trips and one test per validation rule (8.4). */
#include <stdlib.h>
#include <string.h>

#include "crc32.h"
#include "record.h"
#include "tests.h"
#include "unity.h"

static uint8_t buf[1024];

/* Build a record with arbitrary header fields but a CORRECT crc, so each
 * test can break exactly one rule without tripping the CRC check first. */
static size_t make_raw(uint8_t type, uint32_t klen, uint32_t vlen,
                       uint32_t payload_len)
{
    uint8_t *body = buf + REC_HEADER_SIZE;
    uint32_t body_len = REC_BODY_FIXED + payload_len;
    body[0] = type;
    rec_put_u32(body + 1, klen);
    rec_put_u32(body + 5, vlen);
    memset(body + REC_BODY_FIXED, 'x', payload_len);
    rec_put_u32(buf, body_len);
    rec_put_u32(buf + 4, crc32_compute(body, body_len));
    return REC_HEADER_SIZE + body_len;
}

static rec_result decode_buf(size_t avail)
{
    rec_view v;
    size_t used;
    return rec_decode(buf, avail, &v, &used);
}

static void test_set_round_trip(void)
{
    size_t n = rec_encoded_size(4, 5);
    TEST_ASSERT_EQUAL_size_t(8 + 9 + 4 + 5, n);
    rec_encode(buf, REC_SET, "name", 4, "Ahmed", 5);

    rec_view v;
    size_t used = 0;
    TEST_ASSERT_EQUAL_INT(REC_OK, rec_decode(buf, n, &v, &used));
    TEST_ASSERT_EQUAL_size_t(n, used);
    TEST_ASSERT_EQUAL_UINT8(REC_SET, v.type);
    TEST_ASSERT_EQUAL_UINT32(4, v.klen);
    TEST_ASSERT_EQUAL_UINT32(5, v.vlen);
    TEST_ASSERT_EQUAL_MEMORY("name", v.key, 4);
    TEST_ASSERT_EQUAL_MEMORY("Ahmed", v.val, 5);
}

static void test_del_round_trip(void)
{
    size_t n = rec_encoded_size(3, 0);
    rec_encode(buf, REC_DEL, "age", 3, NULL, 0);
    rec_view v;
    size_t used;
    TEST_ASSERT_EQUAL_INT(REC_OK, rec_decode(buf, n, &v, &used));
    TEST_ASSERT_EQUAL_UINT8(REC_DEL, v.type);
    TEST_ASSERT_EQUAL_UINT32(0, v.vlen);
}

static void test_max_size_round_trip(void)
{
    size_t n = rec_encoded_size(CSKV_MAX_KEY_LEN, CSKV_MAX_VALUE_LEN);
    uint8_t *big = malloc(n);
    uint8_t *key = malloc(CSKV_MAX_KEY_LEN);
    uint8_t *val = malloc(CSKV_MAX_VALUE_LEN);
    TEST_ASSERT_NOT_NULL(big);
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_NOT_NULL(val);
    memset(key, 'k', CSKV_MAX_KEY_LEN);
    memset(val, 0xAB, CSKV_MAX_VALUE_LEN);

    rec_encode(big, REC_SET, key, CSKV_MAX_KEY_LEN, val, CSKV_MAX_VALUE_LEN);
    rec_view v;
    size_t used;
    rec_result r = rec_decode(big, n, &v, &used);
    free(big);
    free(key);
    free(val);
    TEST_ASSERT_EQUAL_INT(REC_OK, r);
}

static void test_rule1_truncated_header(void)
{
    rec_encode(buf, REC_SET, "k", 1, "v", 1);
    TEST_ASSERT_EQUAL_INT(REC_ERR_TRUNC_HEADER, decode_buf(7));
    TEST_ASSERT_EQUAL_INT(REC_ERR_TRUNC_HEADER, decode_buf(0));
}

static void test_rule2_impossible_length(void)
{
    rec_put_u32(buf, REC_BODY_FIXED - 1);           /* too small */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTH, decode_buf(sizeof buf));
    rec_put_u32(buf, REC_MAX_BODY + 1);             /* too large */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTH, decode_buf(sizeof buf));
    rec_put_u32(buf, 0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTH, decode_buf(sizeof buf));
}

static void test_rule3_truncated_body(void)
{
    size_t n = make_raw(REC_SET, 2, 3, 5);
    TEST_ASSERT_EQUAL_INT(REC_ERR_TRUNC_BODY, decode_buf(n - 1));
    TEST_ASSERT_EQUAL_INT(REC_ERR_TRUNC_BODY, decode_buf(REC_HEADER_SIZE));
    TEST_ASSERT_EQUAL_INT(REC_OK, decode_buf(n));
}

static void test_rule4_crc_mismatch(void)
{
    size_t n = rec_encoded_size(4, 5);
    rec_encode(buf, REC_SET, "name", 4, "Ahmed", 5);
    buf[n - 1] ^= 0xFF;                             /* flip a value byte */
    TEST_ASSERT_EQUAL_INT(REC_ERR_CRC, decode_buf(n));
}

static void test_rule5_unknown_type(void)
{
    size_t n = make_raw(3, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_TYPE, decode_buf(n));
    n = make_raw(0, 1, 0, 1);
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_TYPE, decode_buf(n));
}

static void test_rule6_inconsistent_lengths(void)
{
    size_t n = make_raw(REC_SET, 0, 2, 2);                  /* empty key */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTHS, decode_buf(n));

    n = make_raw(REC_SET, 3, 2, 6);                         /* sum != body */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTHS, decode_buf(n));

    n = make_raw(REC_SET, CSKV_MAX_KEY_LEN + 1, 0, CSKV_MAX_KEY_LEN + 1);
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTHS, decode_buf(n)); /* key > MAX */

    n = make_raw(REC_SET, 1, CSKV_MAX_VALUE_LEN + 1, 1);    /* vlen > MAX */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTHS, decode_buf(n));

    n = make_raw(REC_SET, 0xFFFFFFFFu, 10, 1);              /* would wrap */
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_LENGTHS, decode_buf(n));
}

static void test_rule7_tombstone_with_value(void)
{
    size_t n = make_raw(REC_DEL, 1, 1, 2);
    TEST_ASSERT_EQUAL_INT(REC_ERR_BAD_TOMBSTONE, decode_buf(n));
}

static void test_little_endian_layout(void)
{
    uint8_t b[8];
    rec_put_u32(b, 0x11223344u);
    TEST_ASSERT_EQUAL_HEX8(0x44, b[0]);
    TEST_ASSERT_EQUAL_HEX8(0x11, b[3]);
    rec_put_u64(b, 0x0102030405060708ull);
    TEST_ASSERT_EQUAL_HEX8(0x08, b[0]);
    TEST_ASSERT_EQUAL_UINT64(0x0102030405060708ull, rec_get_u64(b));
    rec_put_u16(b, 0xBEEF);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, rec_get_u16(b));
}

void run_record_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_set_round_trip);
    RUN_TEST(test_del_round_trip);
    RUN_TEST(test_max_size_round_trip);
    RUN_TEST(test_rule1_truncated_header);
    RUN_TEST(test_rule2_impossible_length);
    RUN_TEST(test_rule3_truncated_body);
    RUN_TEST(test_rule4_crc_mismatch);
    RUN_TEST(test_rule5_unknown_type);
    RUN_TEST(test_rule6_inconsistent_lengths);
    RUN_TEST(test_rule7_tombstone_with_value);
    RUN_TEST(test_little_endian_layout);
}
