/*
 * test_fuzz.c - NFR-5: no crash and no bad data on ANY file content.
 *
 * Random logs and snapshots come from a fixed-seed generator, so every run
 * tests exactly the same inputs and a failure can be reproduced.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "unity.h"

static char dir[TEST_PATH_MAX];
static char log_file[TEST_PATH_MAX];

/* xorshift32: tiny deterministic pseudo-random generator. */
static uint32_t rng_next(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static void write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *fp = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(n, fwrite(data, 1, n, fp));
    fclose(fp);
}

/* Valid header + random tail: open must succeed, account for every byte,
 * and leave exactly the valid prefix on disk. */
static void test_random_log_tails(void)
{
    static uint8_t buf[4096];
    uint32_t seed = 12345;
    test_fresh_dir(dir, "fuzz_tail");
    test_log_path(log_file, dir);

    for (int round = 0; round < 150; round++) {
        size_t n = 8 + rng_next(&seed) % (sizeof buf - 8);
        memcpy(buf, "CSKV\x01\x00\x00\x00", 8);
        for (size_t i = 8; i < n; i++)
            buf[i] = (uint8_t)rng_next(&seed);
        write_file(log_file, buf, n);

        cskv *db = test_open(dir);
        cskv_stats s;
        cskv_get_stats(db, &s);
        cskv_close(db);
        TEST_ASSERT_EQUAL_UINT64(n, s.last_recovery.bytes_valid +
                                        s.last_recovery.bytes_discarded);
        TEST_ASSERT_EQUAL_INT64((long long)s.last_recovery.bytes_valid,
                                test_file_size(log_file));
    }
}

/* Completely random files (header included): OK or BAD_FORMAT, never more. */
static void test_random_whole_files(void)
{
    static uint8_t buf[512];
    uint32_t seed = 777;
    test_fresh_dir(dir, "fuzz_whole");
    test_log_path(log_file, dir);

    for (int round = 0; round < 100; round++) {
        size_t n = 1 + rng_next(&seed) % sizeof buf;
        for (size_t i = 0; i < n; i++)
            buf[i] = (uint8_t)rng_next(&seed);
        write_file(log_file, buf, n);

        cskv *db = NULL;
        cskv_status st = cskv_open(dir, CSKV_SYNC_ALWAYS, &db);
        TEST_ASSERT_TRUE(st == CSKV_OK || st == CSKV_ERR_BAD_FORMAT);
        cskv_close(db);
    }
}

/* Flip one byte at EVERY position of a 3-record log. Records are 21 bytes
 * and start at 8, 29, 50; damage inside record i must keep exactly the
 * records before it. Damage in the header must be BAD_FORMAT. */
static void test_flip_every_byte(void)
{
    test_fresh_dir(dir, "fuzz_flip");
    test_log_path(log_file, dir);
    const long long size = 8 + 3 * 21;

    for (long long off = 0; off < size; off++) {
        test_fresh_dir(dir, "fuzz_flip");
        cskv *db = test_open(dir);
        test_set(db, "k0", "v0");
        test_set(db, "k1", "v1");
        test_set(db, "k2", "v2");
        cskv_close(db);
        test_flip_byte(log_file, off);

        db = NULL;
        cskv_status st = cskv_open(dir, CSKV_SYNC_ALWAYS, &db);
        if (off < 4) {                    /* magic damaged */
            TEST_ASSERT_EQUAL_INT(CSKV_ERR_BAD_FORMAT, st);
            continue;
        }
        if (off < 8) {                    /* version or reserved byte */
            TEST_ASSERT_TRUE(st == CSKV_OK || st == CSKV_ERR_BAD_FORMAT);
            cskv_close(db);
            continue;
        }
        TEST_ASSERT_EQUAL_INT(CSKV_OK, st);
        int damaged = (int)((off - 8) / 21);
        TEST_ASSERT_EQUAL_INT(damaged >= 1, test_has(db, "k0", "v0"));
        TEST_ASSERT_EQUAL_INT(damaged >= 2, test_has(db, "k1", "v1"));
        TEST_ASSERT_FALSE(test_has(db, "k2", "v2"));
        cskv_close(db);
    }
}

/* Random snapshot files and single-byte mutations of a real snapshot must be
 * rejected without touching the live data. */
static void test_random_snapshots_rejected(void)
{
    static uint8_t buf[600];
    char snap[TEST_PATH_MAX + 16], before[128], after[128];
    uint32_t seed = 4242;
    test_fresh_dir(dir, "fuzz_snap");
    snprintf(snap, sizeof snap, "%s/fuzz.snap", dir);
    cskv *db = test_open(dir);
    test_set(db, "keep", "me");
    test_dump(db, before, sizeof before);

    for (int round = 0; round < 100; round++) {
        size_t n = rng_next(&seed) % sizeof buf;
        for (size_t i = 0; i < n; i++)
            buf[i] = (uint8_t)rng_next(&seed);
        if (round % 2 == 0 && n >= 4)
            memcpy(buf, "CSNP", 4);       /* get past the magic check */
        write_file(snap, buf, n);
        TEST_ASSERT_EQUAL_INT(CSKV_ERR_CORRUPT, cskv_restore(db, snap));
    }

    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap));
    long long size = test_file_size(snap);
    for (long long off = 0; off < size; off++) {
        test_flip_byte(snap, off);
        TEST_ASSERT_EQUAL_INT(CSKV_ERR_CORRUPT, cskv_restore(db, snap));
        test_flip_byte(snap, off);        /* undo */
    }
    test_dump(db, after, sizeof after);
    TEST_ASSERT_EQUAL_STRING(before, after);
    cskv_close(db);
}

void run_fuzz_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_random_log_tails);
    RUN_TEST(test_random_whole_files);
    RUN_TEST(test_flip_every_byte);
    RUN_TEST(test_random_snapshots_rejected);
}
