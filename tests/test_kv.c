/* test_kv.c - Public API: set/get/del/keys/stats, limits, lock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "tests.h"
#include "unity.h"

static char dir[TEST_PATH_MAX];

static void test_set_get_del(void)
{
    test_fresh_dir(dir, "kv_basic");
    cskv *db = test_open(dir);
    test_set(db, "name", "Ahmed");
    test_set(db, "age", "21");
    TEST_ASSERT_TRUE(test_has(db, "name", "Ahmed"));

    test_set(db, "name", "Mona");                  /* overwrite */
    TEST_ASSERT_TRUE(test_has(db, "name", "Mona"));

    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_del(db, "age", 3));
    TEST_ASSERT_FALSE(test_has(db, "age", "21"));
    cskv_close(db);
}

static void test_get_missing(void)
{
    test_fresh_dir(dir, "kv_missing");
    cskv *db = test_open(dir);
    void *v = NULL;
    size_t vlen = 0;
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_NOT_FOUND, cskv_get(db, "x", 1, &v, &vlen));
    TEST_ASSERT_NULL(v);
    cskv_close(db);
}

static void test_del_missing_writes_nothing(void)
{
    test_fresh_dir(dir, "kv_del_missing");
    cskv *db = test_open(dir);
    cskv_stats before, after;
    cskv_get_stats(db, &before);
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_NOT_FOUND, cskv_del(db, "nope", 4));
    cskv_get_stats(db, &after);
    TEST_ASSERT_EQUAL_UINT64(before.log_bytes, after.log_bytes);
    TEST_ASSERT_EQUAL_UINT64(before.log_records, after.log_records);
    cskv_close(db);
}

static void test_empty_value_allowed(void)
{
    test_fresh_dir(dir, "kv_empty_val");
    cskv *db = test_open(dir);
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_set(db, "e", 1, NULL, 0));
    cskv_close(db);

    db = test_open(dir);                           /* survives a restart */
    TEST_ASSERT_TRUE(test_has(db, "e", ""));
    cskv_close(db);
}

static void test_max_sizes_and_oversize(void)
{
    test_fresh_dir(dir, "kv_limits");
    cskv *db = test_open(dir);
    char key[CSKV_MAX_KEY_LEN + 1];
    memset(key, 'k', sizeof key);
    char *val = malloc(CSKV_MAX_VALUE_LEN + 1);
    TEST_ASSERT_NOT_NULL(val);
    memset(val, 'v', CSKV_MAX_VALUE_LEN + 1);

    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_set(db, key, CSKV_MAX_KEY_LEN,
                                            val, CSKV_MAX_VALUE_LEN));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG,
                          cskv_set(db, key, CSKV_MAX_KEY_LEN + 1, "v", 1));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG,
                          cskv_set(db, "k", 1, val, CSKV_MAX_VALUE_LEN + 1));
    cskv_close(db);

    db = test_open(dir);
    void *got = NULL;
    size_t glen = 0;
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_get(db, key, CSKV_MAX_KEY_LEN,
                                            &got, &glen));
    TEST_ASSERT_EQUAL_size_t(CSKV_MAX_VALUE_LEN, glen);
    TEST_ASSERT_EQUAL_MEMORY(val, got, glen);
    free(got);
    free(val);
    cskv_close(db);
}

static void test_invalid_args_rejected(void)
{
    test_fresh_dir(dir, "kv_args");
    cskv *db = test_open(dir);
    void *v;
    size_t vl;
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_set(db, "", 0, "v", 1));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_set(db, NULL, 1, "v", 1));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_set(db, "k", 1, NULL, 1));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_set(NULL, "k", 1, "v", 1));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_get(db, "k", 1, NULL, &vl));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_get(db, "", 0, &v, &vl));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_del(db, NULL, 0));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG, cskv_keys(db, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_INVALID_ARG,
                          cskv_open(NULL, CSKV_SYNC_ALWAYS, &db));
    cskv_close(db);
}

/* Appends each key to a comma-separated string. */
static int join_keys(const void *key, size_t klen, void *user)
{
    char *out = user;
    strncat(out, key, klen);
    strcat(out, ",");
    return 0;
}

static void test_keys_sorted(void)
{
    test_fresh_dir(dir, "kv_keys");
    cskv *db = test_open(dir);
    test_set(db, "b", "1");
    test_set(db, "ab", "1");
    test_set(db, "a", "1");
    test_set(db, "c", "1");
    cskv_del(db, "c", 1);
    char out[64] = "";
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_keys(db, join_keys, out));
    TEST_ASSERT_EQUAL_STRING("a,ab,b,", out);
    cskv_close(db);
}

static void test_stats_count_dead_records(void)
{
    test_fresh_dir(dir, "kv_stats");
    cskv *db = test_open(dir);
    test_set(db, "a", "1");
    test_set(db, "b", "1");
    test_set(db, "a", "2");          /* overwrites a: 1 dead */
    cskv_del(db, "b", 1);            /* old b + tombstone: 2 dead */
    cskv_stats s;
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(1, s.live_keys);
    TEST_ASSERT_EQUAL_UINT64(4, s.log_records);
    TEST_ASSERT_EQUAL_UINT64(3, s.dead_records);
    cskv_close(db);

    db = test_open(dir);             /* same numbers after replay */
    cskv_get_stats(db, &s);
    TEST_ASSERT_EQUAL_UINT64(4, s.log_records);
    TEST_ASSERT_EQUAL_UINT64(3, s.dead_records);
    cskv_close(db);
}

static void test_second_open_is_locked(void)
{
    test_fresh_dir(dir, "kv_lock");
    cskv *db = test_open(dir);
    cskv *other = NULL;
    TEST_ASSERT_EQUAL_INT(CSKV_ERR_LOCKED,
                          cskv_open(dir, CSKV_SYNC_ALWAYS, &other));
    TEST_ASSERT_NULL(other);
    cskv_close(db);

    db = test_open(dir);             /* lock released on close */
    cskv_close(db);
}

static void test_leftover_tmp_removed_on_open(void)
{
    char tmp[TEST_PATH_MAX + 16];
    test_fresh_dir(dir, "kv_tmp");
    snprintf(tmp, sizeof tmp, "%s/cskv.log.tmp", dir);
    test_append_bytes(tmp, "half-written compaction", 23);
    cskv *db = test_open(dir);
    TEST_ASSERT_FALSE(plat_file_exists(tmp));
    cskv_close(db);
}

static void test_missing_dir_is_created(void)
{
    test_fresh_dir(dir, "kv_newdir");
    strcat(dir, "/nested/deeper");
    cskv *db = test_open(dir);
    test_set(db, "k", "v");
    cskv_close(db);
}

void run_kv_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_set_get_del);
    RUN_TEST(test_get_missing);
    RUN_TEST(test_del_missing_writes_nothing);
    RUN_TEST(test_empty_value_allowed);
    RUN_TEST(test_max_sizes_and_oversize);
    RUN_TEST(test_invalid_args_rejected);
    RUN_TEST(test_keys_sorted);
    RUN_TEST(test_stats_count_dead_records);
    RUN_TEST(test_second_open_is_locked);
    RUN_TEST(test_leftover_tmp_removed_on_open);
    RUN_TEST(test_missing_dir_is_created);
}
