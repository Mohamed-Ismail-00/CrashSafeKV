/* test_compact.c - COMPACT keeps the data, shrinks the log, survives crashes. */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "tests.h"
#include "unity.h"

static char dir[TEST_PATH_MAX];
static char log_file[TEST_PATH_MAX];

/* 5 keys, 2 overwritten, 1 deleted -> 4 live keys, 8 records (demo 9.4). */
static cskv *make_store_with_dead_records(const char *name)
{
    test_fresh_dir(dir, name);
    test_log_path(log_file, dir);
    cskv *db = test_open(dir);
    test_set(db, "a", "1");
    test_set(db, "b", "2");
    test_set(db, "c", "3");
    test_set(db, "d", "4");
    test_set(db, "e", "5");
    test_set(db, "a", "one");
    test_set(db, "b", "two");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_del(db, "e", 1));
    return db;
}

static void test_data_identical_and_log_shrinks(void)
{
    char before[256], after[256];
    cskv *db = make_store_with_dead_records("cmp_basic");
    cskv_stats s1, s2;
    cskv_get_stats(db, &s1);
    TEST_ASSERT_EQUAL_UINT64(4, s1.dead_records);
    test_dump(db, before, sizeof before);

    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_compact(db));
    cskv_get_stats(db, &s2);
    test_dump(db, after, sizeof after);

    TEST_ASSERT_EQUAL_STRING(before, after);
    TEST_ASSERT_EQUAL_STRING("a=one;b=two;c=3;d=4;", after);
    TEST_ASSERT_TRUE(s2.log_bytes < s1.log_bytes);
    TEST_ASSERT_EQUAL_UINT64(4, s2.log_records);
    TEST_ASSERT_EQUAL_UINT64(0, s2.dead_records);
    cskv_close(db);
    TEST_ASSERT_EQUAL_INT64((long long)s2.log_bytes, test_file_size(log_file));
}

static void test_compacted_log_survives_restart_and_appends(void)
{
    char state[256];
    cskv *db = make_store_with_dead_records("cmp_restart");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_compact(db));
    test_set(db, "f", "6");                  /* append after compaction */
    cskv_close(db);

    db = test_open(dir);
    cskv_stats s;
    cskv_get_stats(db, &s);
    test_dump(db, state, sizeof state);
    TEST_ASSERT_EQUAL_STRING("a=one;b=two;c=3;d=4;f=6;", state);
    TEST_ASSERT_EQUAL_INT(0, s.last_recovery.corruption_found);
    TEST_ASSERT_EQUAL_UINT64(5, s.last_recovery.records_replayed);
    cskv_close(db);
}

static void test_compact_empty_store(void)
{
    test_fresh_dir(dir, "cmp_empty");
    test_log_path(log_file, dir);
    cskv *db = test_open(dir);
    test_set(db, "x", "1");
    cskv_del(db, "x", 1);
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_compact(db));
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(0, s.live_keys);
    TEST_ASSERT_EQUAL_UINT64(8, s.log_bytes);   /* header only */
    cskv_close(db);
    TEST_ASSERT_EQUAL_INT64(8, test_file_size(log_file));
}

/* Crash in the middle of compaction = the process dies after writing part
 * of cskv.log.tmp but before the rename. The original log must win. */
static void test_crash_during_compaction_keeps_original(void)
{
    char before[256], after[256], tmp[TEST_PATH_MAX + 16];
    cskv *db = make_store_with_dead_records("cmp_crash");
    test_dump(db, before, sizeof before);
    cskv_close(db);

    snprintf(tmp, sizeof tmp, "%s/cskv.log.tmp", dir);
    test_append_bytes(tmp, "CSKV\x01\x00\x00\x00 partial", 16);

    db = test_open(dir);
    test_dump(db, after, sizeof after);
    TEST_ASSERT_EQUAL_STRING(before, after);
    TEST_ASSERT_FALSE(plat_file_exists(tmp));
    cskv_close(db);
}

void run_compact_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_data_identical_and_log_shrinks);
    RUN_TEST(test_compacted_log_survives_restart_and_appends);
    RUN_TEST(test_compact_empty_store);
    RUN_TEST(test_crash_during_compaction_keeps_original);
}
