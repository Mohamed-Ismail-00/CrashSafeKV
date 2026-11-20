/* test_snapshot.c - SNAPSHOT / RESTORE round trips and rejected snapshots. */
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "tests.h"
#include "unity.h"

static char dir[TEST_PATH_MAX];
static char snap[TEST_PATH_MAX + 32];

static cskv *open_fresh(const char *name)
{
    test_fresh_dir(dir, name);
    snprintf(snap, sizeof snap, "%s/backup.snap", dir);
    if (plat_file_exists(snap))
        plat_remove(snap);
    return test_open(dir);
}

static void test_save_restore_round_trip(void)
{
    char saved[256], now[256];
    cskv *db = open_fresh("snap_round");
    test_set(db, "name", "Ahmed");
    test_set(db, "age", "21");
    test_set(db, "city", "Cairo");
    test_dump(db, saved, sizeof saved);
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap));

    /* Change everything after the snapshot. */
    test_set(db, "name", "Mona");
    cskv_del(db, "age", 3);
    test_set(db, "extra", "x");

    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_restore(db, snap));
    test_dump(db, now, sizeof now);
    TEST_ASSERT_EQUAL_STRING(saved, now);
    cskv_close(db);

    /* The restored state is the new log: it survives a restart. */
    db = test_open(dir);
    test_dump(db, now, sizeof now);
    TEST_ASSERT_EQUAL_STRING(saved, now);
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(0, s.dead_records);
    cskv_close(db);
}

static void test_no_tmp_left_after_save(void)
{
    char tmp[TEST_PATH_MAX + 48];
    cskv *db = open_fresh("snap_tmp");
    test_set(db, "k", "v");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap));
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap)); /* overwrite */
    snprintf(tmp, sizeof tmp, "%s.tmp", snap);
    TEST_ASSERT_TRUE(plat_file_exists(snap));
    TEST_ASSERT_FALSE(plat_file_exists(tmp));
    cskv_close(db);
}

/* Damage the snapshot in some way, then check RESTORE refuses it and the
 * live state is exactly what it was. */
static void expect_rejected(cskv *db, cskv_status expected)
{
    char before[256], after[256];
    cskv_stats s1, s2;
    test_dump(db, before, sizeof before);
    cskv_get_stats(db, &s1);

    TEST_ASSERT_EQUAL_INT(expected, cskv_restore(db, snap));

    test_dump(db, after, sizeof after);
    cskv_get_stats(db, &s2);
    TEST_ASSERT_EQUAL_STRING(before, after);
    TEST_ASSERT_EQUAL_UINT64(s1.log_bytes, s2.log_bytes);
}

static void test_corrupted_snapshot_rejected(void)
{
    cskv *db = open_fresh("snap_corrupt");
    test_set(db, "a", "1");
    test_set(db, "b", "2");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap));
    test_set(db, "c", "3");                        /* state differs now */

    test_flip_byte(snap, 20);                      /* inside an entry */
    expect_rejected(db, CSKV_ERR_CORRUPT);
    test_flip_byte(snap, 20);                      /* undo */

    test_flip_byte(snap, 0);                       /* wrong magic */
    expect_rejected(db, CSKV_ERR_CORRUPT);
    test_flip_byte(snap, 0);

    test_cut_bytes(snap, 3);                       /* truncated */
    expect_rejected(db, CSKV_ERR_CORRUPT);
    cskv_close(db);
}

static void test_missing_or_tiny_snapshot(void)
{
    cskv *db = open_fresh("snap_missing");
    test_set(db, "a", "1");
    expect_rejected(db, CSKV_ERR_IO);              /* file does not exist */

    test_append_bytes(snap, "CSNP", 4);
    expect_rejected(db, CSKV_ERR_CORRUPT);
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_restore(db, ""));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_snapshot(db, NULL));
    cskv_close(db);
}

static void test_snapshot_of_empty_store(void)
{
    char state[64];
    cskv *db = open_fresh("snap_empty");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_snapshot(db, snap));
    TEST_ASSERT_EQUAL_INT64(20, test_file_size(snap));   /* header + crc */

    test_set(db, "k", "v");
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_restore(db, snap));
    test_dump(db, state, sizeof state);
    TEST_ASSERT_EQUAL_STRING("", state);
    cskv_close(db);
}

void run_snapshot_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_save_restore_round_trip);
    RUN_TEST(test_no_tmp_left_after_save);
    RUN_TEST(test_corrupted_snapshot_rejected);
    RUN_TEST(test_missing_or_tiny_snapshot);
    RUN_TEST(test_snapshot_of_empty_store);
}
