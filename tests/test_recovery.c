/*
 * test_recovery.c - Replay and corruption handling (CLAUDE.md 10.1).
 *
 * Most tests write 10 records "k0".."k9" = "v0".."v9". Each record is
 * 8 (header) + 9 (type/klen/vlen) + 2 (key) + 2 (value) = 21 bytes, so
 * record i starts at offset 8 + 21 * i.
 */
#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "unity.h"

#define REC_SIZE 21
#define N_RECS   10

static char dir[TEST_PATH_MAX];
static char log_file[TEST_PATH_MAX];

static long long rec_offset(int i)
{
    return 8 + (long long)REC_SIZE * i;
}

/* Fresh store with k0..k9 written and closed. */
static void write_ten(const char *name)
{
    test_fresh_dir(dir, name);
    test_log_path(log_file, dir);
    cskv *db = test_open(dir);
    char k[8], v[8];
    for (int i = 0; i < N_RECS; i++) {
        snprintf(k, sizeof k, "k%d", i);
        snprintf(v, sizeof v, "v%d", i);
        test_set(db, k, v);
    }
    cskv_close(db);
}

/* Reopen, check that exactly keys k0..k(kept-1) survived. */
static cskv_recovery_info reopen_expect(int kept)
{
    cskv *db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    char k[8], v[8];
    for (int i = 0; i < N_RECS; i++) {
        snprintf(k, sizeof k, "k%d", i);
        snprintf(v, sizeof v, "v%d", i);
        TEST_ASSERT_EQUAL_INT_MESSAGE(i < kept, test_has(db, k, v), k);
    }
    TEST_ASSERT_EQUAL_UINT64(kept, s.live_keys);
    cskv_close(db);
    /* The corrupted tail must be gone from the file itself. */
    TEST_ASSERT_EQUAL_INT64(rec_offset(kept), test_file_size(log_file));
    return s.last_recovery;
}

static void assert_reason(const cskv_recovery_info *r, const char *rule,
                          long long offset)
{
    char expected[128];
    snprintf(expected, sizeof expected, "%s at offset %lld", rule, offset);
    TEST_ASSERT_EQUAL_INT(1, r->corruption_found);
    TEST_ASSERT_EQUAL_STRING(expected, r->reason);
}

static void test_new_store_has_header_only(void)
{
    test_fresh_dir(dir, "rec_new");
    test_log_path(log_file, dir);
    cskv *db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(0, s.last_recovery.records_replayed);
    TEST_ASSERT_EQUAL_INT(0, s.last_recovery.corruption_found);
    cskv_close(db);
    TEST_ASSERT_EQUAL_INT64(8, test_file_size(log_file));

    /* Header-only log: reopens cleanly with nothing in it. */
    db = test_open(dir);
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(0, s.live_keys);
    TEST_ASSERT_EQUAL_UINT64(8, s.last_recovery.bytes_valid);
    cskv_close(db);
}

static void test_clean_replay_of_1000_records(void)
{
    test_fresh_dir(dir, "rec_1000");
    cskv *db = test_open(dir);
    char k[16];
    for (int i = 0; i < 1000; i++) {
        snprintf(k, sizeof k, "key%04d", i);
        test_set(db, k, "value");
    }
    cskv_close(db);

    db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(1000, s.last_recovery.records_replayed);
    TEST_ASSERT_EQUAL_UINT64(1000, s.live_keys);
    TEST_ASSERT_EQUAL_INT(0, s.last_recovery.corruption_found);
    TEST_ASSERT_TRUE(test_has(db, "key0999", "value"));
    cskv_close(db);
}

static void test_truncated_header(void)
{
    write_ten("rec_trunc_hdr");
    test_cut_bytes(log_file, REC_SIZE - 3);      /* 3 bytes of k9 remain */
    cskv_recovery_info r = reopen_expect(9);
    assert_reason(&r, "truncated header", rec_offset(9));
    TEST_ASSERT_EQUAL_UINT64(3, r.bytes_discarded);
}

static void test_truncated_body(void)
{
    write_ten("rec_trunc_body");
    test_cut_bytes(log_file, 5);
    cskv_recovery_info r = reopen_expect(9);
    assert_reason(&r, "truncated body", rec_offset(9));
    TEST_ASSERT_EQUAL_UINT64(REC_SIZE - 5, r.bytes_discarded);
}

static void test_crc_flip_in_middle_record(void)
{
    write_ten("rec_crc");
    test_flip_byte(log_file, rec_offset(5) + 19); /* inside k5's key */
    cskv_recovery_info r = reopen_expect(5);      /* k0..k4 kept */
    assert_reason(&r, "CRC mismatch", rec_offset(5));
    TEST_ASSERT_EQUAL_UINT64(5 * REC_SIZE, r.bytes_discarded);
    TEST_ASSERT_EQUAL_UINT64(5, r.records_replayed);
}

static void test_garbage_appended(void)
{
    const unsigned char junk[30] = {0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3};
    write_ten("rec_garbage");
    test_append_bytes(log_file, junk, sizeof junk);
    cskv_recovery_info r = reopen_expect(N_RECS);
    assert_reason(&r, "impossible length", rec_offset(N_RECS));
    TEST_ASSERT_EQUAL_UINT64(sizeof junk, r.bytes_discarded);
}

static void test_wrong_magic_is_bad_format(void)
{
    write_ten("rec_magic");
    test_flip_byte(log_file, 0);
    cskv *db = NULL;
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_BAD_FORMAT,
                          cskv_open(dir, CSKV_SYNC_ALWAYS, &db));
    TEST_ASSERT_NULL(db);
}

static void test_short_file_is_bad_format(void)
{
    test_fresh_dir(dir, "rec_short");
    test_log_path(log_file, dir);
    test_append_bytes(log_file, "CSK", 3);
    cskv *db = NULL;
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_BAD_FORMAT,
                          cskv_open(dir, CSKV_SYNC_ALWAYS, &db));
}

static void test_writes_after_recovery_survive(void)
{
    write_ten("rec_after");
    test_cut_bytes(log_file, 4);                 /* break k9 */
    cskv *db = test_open(dir);
    test_set(db, "new", "data");
    cskv_close(db);

    db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_INT(0, s.last_recovery.corruption_found);
    TEST_ASSERT_TRUE(test_has(db, "k8", "v8"));
    TEST_ASSERT_FALSE(test_has(db, "k9", "v9"));
    TEST_ASSERT_TRUE(test_has(db, "new", "data"));
    cskv_close(db);
}

static void test_100k_records_recover_under_2s(void)
{
    test_fresh_dir(dir, "rec_100k");
    cskv *db = NULL;
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_open(dir, CSKV_SYNC_NONE, &db));
    char k[16];
    for (int i = 0; i < 100000; i++) {
        snprintf(k, sizeof k, "key%06d", i);
        test_set(db, k, "some value of moderate size");
    }
    cskv_close(db);

    db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(100000, s.last_recovery.records_replayed);
    TEST_ASSERT_TRUE_MESSAGE(s.last_recovery.duration_ms < 2000.0,
                             "NFR-4: recovery must take under 2 seconds");
    cskv_close(db);
}

void run_recovery_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_new_store_has_header_only);
    RUN_TEST(test_clean_replay_of_1000_records);
    RUN_TEST(test_truncated_header);
    RUN_TEST(test_truncated_body);
    RUN_TEST(test_crc_flip_in_middle_record);
    RUN_TEST(test_garbage_appended);
    RUN_TEST(test_wrong_magic_is_bad_format);
    RUN_TEST(test_short_file_is_bad_format);
    RUN_TEST(test_writes_after_recovery_survive);
    RUN_TEST(test_100k_records_recover_under_2s);
}
