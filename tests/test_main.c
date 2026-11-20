/* test_main.c - Entry point for build/cskv_tests.exe (Unity). */
#include "unity.h"
#include "tests.h"

#ifdef CSKV_MEMTRACK
#include "memtrack.h"
#endif

#ifdef CSKV_MEMTRACK
/* In the leak-check build every test must free everything it allocated.
 * Counting per test means only the leaking test fails. */
static long live_at_start;

void setUp(void)
{
    live_at_start = mt_live();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(live_at_start, mt_live(),
                                  "memory leak: allocations not freed");
}
#else
void setUp(void) {}
void tearDown(void) {}
#endif

int main(void)
{
    UNITY_BEGIN();
    run_crc32_tests();
    run_hashtable_tests();
    run_record_tests();
    run_recovery_tests();
    run_kv_tests();
    run_compact_tests();
    run_snapshot_tests();
    run_fuzz_tests();
    return UNITY_END();
}
