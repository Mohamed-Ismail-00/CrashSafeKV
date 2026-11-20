/* test_crc32.c - CRC32 known vectors and incremental use. */
#include <string.h>

#include "crc32.h"
#include "tests.h"
#include "unity.h"

static void test_known_vector(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32_compute("123456789", 9));
}

static void test_empty_input_is_zero(void)
{
    TEST_ASSERT_EQUAL_HEX32(0u, crc32_compute("", 0));
}

static void test_incremental_equals_one_shot(void)
{
    const char *msg = "The quick brown fox jumps over the lazy dog";
    size_t n = strlen(msg);
    uint32_t crc = 0;

    /* Feed the message in uneven chunks. */
    crc = crc32_update(crc, msg, 5);
    crc = crc32_update(crc, msg + 5, 17);
    crc = crc32_update(crc, msg + 22, n - 22);

    TEST_ASSERT_EQUAL_HEX32(crc32_compute(msg, n), crc);
    TEST_ASSERT_EQUAL_HEX32(0x414FA339u, crc);
}

static void test_single_bit_change_detected(void)
{
    char a[] = "hello world";
    uint32_t before = crc32_compute(a, sizeof a - 1);
    a[3] ^= 0x01;
    TEST_ASSERT_NOT_EQUAL(before, crc32_compute(a, sizeof a - 1));
}

void run_crc32_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_known_vector);
    RUN_TEST(test_empty_input_is_zero);
    RUN_TEST(test_incremental_equals_one_shot);
    RUN_TEST(test_single_bit_change_detected);
}
