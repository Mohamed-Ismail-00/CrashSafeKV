/* tests.h - Test runners and small shared helpers (test_util.c). */
#ifndef CSKV_TESTS_H
#define CSKV_TESTS_H

#include <stddef.h>

#include "cskv.h"

/* One runner per test file; test_main.c calls them all. */
void run_crc32_tests(void);
void run_hashtable_tests(void);
void run_record_tests(void);
void run_recovery_tests(void);
void run_kv_tests(void);
void run_compact_tests(void);
void run_snapshot_tests(void);
void run_fuzz_tests(void);

/* Every test works in its own folder under build/test_data. */
#define TEST_PATH_MAX 260

/* Make build/test_data/<name> exist and contain no store files. */
void test_fresh_dir(char *out, const char *name);
/* <dir>/cskv.log */
void test_log_path(char *out, const char *dir);

/* Open a store and fail the test if that does not work. */
cskv *test_open(const char *dir);
/* SET with C strings. */
void test_set(cskv *db, const char *key, const char *val);
/* 1 if key exists with exactly this value. */
int test_has(cskv *db, const char *key, const char *val);
/* Whole state as text, sorted: "a=1;b=2;" (for before/after comparisons). */
void test_dump(cskv *db, char *out, size_t cap);

/* Raw file damage, used to simulate crashes. */
long long test_file_size(const char *path);
void test_cut_bytes(const char *path, long long n);       /* drop last n bytes */
void test_flip_byte(const char *path, long long offset);  /* XOR with 0xFF */
void test_append_bytes(const char *path, const void *data, size_t n);

#endif /* CSKV_TESTS_H */
