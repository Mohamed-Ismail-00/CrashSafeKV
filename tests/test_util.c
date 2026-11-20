/* test_util.c - Helpers shared by the store-level tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "tests.h"
#include "unity.h"

void test_fresh_dir(char *out, const char *name)
{
    char path[TEST_PATH_MAX];
    snprintf(out, TEST_PATH_MAX, "build/test_data/%s", name);
    TEST_ASSERT_EQUAL_INT(0, plat_mkdir_p(out));

    const char *files[] = {"cskv.log", "cskv.log.tmp", "cskv.lock"};
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", out, files[i]);
        if (plat_file_exists(path))
            plat_remove(path);
    }
}

void test_log_path(char *out, const char *dir)
{
    snprintf(out, TEST_PATH_MAX, "%s/cskv.log", dir);
}

cskv *test_open(const char *dir)
{
    cskv *db = NULL;
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_open(dir, CSKV_SYNC_ALWAYS, &db));
    TEST_ASSERT_NOT_NULL(db);
    return db;
}

void test_set(cskv *db, const char *key, const char *val)
{
    TEST_ASSERT_EQUAL_INT(CSKV_OK,
                          cskv_set(db, key, strlen(key), val, strlen(val)));
}

int test_has(cskv *db, const char *key, const char *val)
{
    void *v = NULL;
    size_t vlen = 0;
    if (cskv_get(db, key, strlen(key), &v, &vlen) != CSKV_OK)
        return 0;
    int same = vlen == strlen(val) && memcmp(v, val, vlen) == 0;
    free(v);
    return same;
}

typedef struct {
    cskv *db;
    char *out;
    size_t cap;
} dump_ctx;

static int dump_one(const void *key, size_t klen, void *user)
{
    dump_ctx *d = user;
    void *v = NULL;
    size_t vlen = 0;
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_get(d->db, key, klen, &v, &vlen));
    size_t used = strlen(d->out);
    snprintf(d->out + used, d->cap - used, "%.*s=%.*s;",
             (int)klen, (const char *)key, (int)vlen, (const char *)v);
    free(v);
    return 0;
}

void test_dump(cskv *db, char *out, size_t cap)
{
    dump_ctx d = {db, out, cap};
    out[0] = '\0';
    TEST_ASSERT_EQUAL_INT(CSKV_OK, cskv_keys(db, dump_one, &d));
}

long long test_file_size(const char *path)
{
    FILE *fp = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(fp);
    long long size = plat_file_size(fp);
    fclose(fp);
    return size;
}

void test_cut_bytes(const char *path, long long n)
{
    FILE *fp = fopen(path, "r+b");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, plat_truncate(fp, plat_file_size(fp) - n));
    fclose(fp);
}

void test_flip_byte(const char *path, long long offset)
{
    FILE *fp = fopen(path, "r+b");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_INT(0, fseek(fp, (long)offset, SEEK_SET));
    int c = fgetc(fp);
    TEST_ASSERT_NOT_EQUAL(EOF, c);
    fseek(fp, (long)offset, SEEK_SET);     /* required between read and write */
    fputc(c ^ 0xFF, fp);
    fclose(fp);
}

void test_append_bytes(const char *path, const void *data, size_t n)
{
    FILE *fp = fopen(path, "ab");
    TEST_ASSERT_NOT_NULL(fp);
    TEST_ASSERT_EQUAL_size_t(n, fwrite(data, 1, n, fp));
    fclose(fp);
}
