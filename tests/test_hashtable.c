/* test_hashtable.c - put/get/overwrite/remove/resize/binary keys. */
#include <stdio.h>
#include <string.h>

#include "hashtable.h"
#include "tests.h"
#include "unity.h"

/* Shorthand for C-string keys/values. */
static int put_s(ht *t, const char *k, const char *v)
{
    return ht_put(t, k, strlen(k), v, strlen(v));
}

static void assert_value(ht *t, const char *k, const char *expected)
{
    size_t vlen = 0;
    const void *v = ht_get(t, k, strlen(k), &vlen);
    TEST_ASSERT_NOT_NULL(v);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), vlen);
    TEST_ASSERT_EQUAL_MEMORY(expected, v, vlen);
}

static void test_put_get(void)
{
    ht *t = ht_create();
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_EQUAL_INT(0, put_s(t, "name", "Ahmed"));
    TEST_ASSERT_EQUAL_INT(0, put_s(t, "age", "21"));
    assert_value(t, "name", "Ahmed");
    assert_value(t, "age", "21");
    TEST_ASSERT_NULL(ht_get(t, "missing", 7, NULL));
    TEST_ASSERT_EQUAL_size_t(2, ht_count(t));
    ht_destroy(t);
}

static void test_overwrite_replaces_value(void)
{
    ht *t = ht_create();
    put_s(t, "k", "first value");
    put_s(t, "k", "v2");
    assert_value(t, "k", "v2");
    TEST_ASSERT_EQUAL_size_t(1, ht_count(t));
    ht_destroy(t);
}

static void test_remove_and_remove_missing(void)
{
    ht *t = ht_create();
    put_s(t, "a", "1");
    put_s(t, "b", "2");
    TEST_ASSERT_EQUAL_INT(1, ht_remove(t, "a", 1));
    TEST_ASSERT_NULL(ht_get(t, "a", 1, NULL));
    TEST_ASSERT_EQUAL_INT(0, ht_remove(t, "a", 1));
    TEST_ASSERT_EQUAL_INT(0, ht_remove(t, "zzz", 3));
    assert_value(t, "b", "2");
    TEST_ASSERT_EQUAL_size_t(1, ht_count(t));
    ht_destroy(t);
}

static void test_empty_value_is_stored(void)
{
    ht *t = ht_create();
    size_t vlen = 99;
    TEST_ASSERT_EQUAL_INT(0, ht_put(t, "e", 1, "", 0));
    TEST_ASSERT_NOT_NULL(ht_get(t, "e", 1, &vlen));
    TEST_ASSERT_EQUAL_size_t(0, vlen);
    ht_destroy(t);
}

static void test_resize_past_10000_keys(void)
{
    enum { N = 12000 };
    char k[32], v[32];
    ht *t = ht_create();

    for (int i = 0; i < N; i++) {
        snprintf(k, sizeof k, "key-%d", i);
        snprintf(v, sizeof v, "val-%d", i);
        TEST_ASSERT_EQUAL_INT(0, put_s(t, k, v));
    }
    TEST_ASSERT_EQUAL_size_t(N, ht_count(t));
    for (int i = 0; i < N; i++) {
        snprintf(k, sizeof k, "key-%d", i);
        snprintf(v, sizeof v, "val-%d", i);
        assert_value(t, k, v);
    }
    ht_destroy(t);
}

static void test_binary_keys_with_nul(void)
{
    const char k1[] = {'a', '\0', 'b'};
    const char k2[] = {'a', '\0', 'c'};
    ht *t = ht_create();
    ht_put(t, k1, sizeof k1, "one", 3);
    ht_put(t, k2, sizeof k2, "two", 3);
    size_t vlen;
    TEST_ASSERT_EQUAL_MEMORY("one", ht_get(t, k1, sizeof k1, &vlen), 3);
    TEST_ASSERT_EQUAL_MEMORY("two", ht_get(t, k2, sizeof k2, &vlen), 3);
    /* "a" alone is a different key from "a\0b". */
    TEST_ASSERT_NULL(ht_get(t, "a", 1, NULL));
    ht_destroy(t);
}

static int count_visit(const void *k, size_t kl, const void *v, size_t vl,
                       void *user)
{
    (void)k; (void)kl; (void)v; (void)vl;
    int *n = user;
    return ++*n == 3 ? 7 : 0;   /* stop after the third entry */
}

static void test_foreach_visits_and_stops_early(void)
{
    ht *t = ht_create();
    put_s(t, "a", "1");
    put_s(t, "b", "2");
    put_s(t, "c", "3");
    put_s(t, "d", "4");
    int n = 0;
    TEST_ASSERT_EQUAL_INT(7, ht_foreach(t, count_visit, &n));
    TEST_ASSERT_EQUAL_INT(3, n);
    ht_destroy(t);
}

static void test_destroy_null_and_with_entries(void)
{
    ht_destroy(NULL);
    ht *t = ht_create();
    put_s(t, "x", "y");
    ht_destroy(t);   /* leaks would show up under Dr. Memory */
}

void run_hashtable_tests(void)
{
    Unity.TestFile = __FILE__;
    RUN_TEST(test_put_get);
    RUN_TEST(test_overwrite_replaces_value);
    RUN_TEST(test_remove_and_remove_missing);
    RUN_TEST(test_empty_value_is_stored);
    RUN_TEST(test_resize_past_10000_keys);
    RUN_TEST(test_binary_keys_with_nul);
    RUN_TEST(test_foreach_visits_and_stops_early);
    RUN_TEST(test_destroy_null_and_with_entries);
}
