/*
 * memtrack.c - Counting wrappers for the leak-check build (see memtrack.h).
 * Compiled WITHOUT the redirecting macros, so it calls the real allocator.
 * The counter is test-only global state; the engine itself has none.
 */
#include <stdlib.h>

#define CSKV_MEMTRACK
#include "memtrack.h"
#undef malloc
#undef calloc
#undef realloc
#undef free

static long live_count;

void *mt_malloc(size_t n)
{
    void *p = malloc(n);
    if (p != NULL)
        live_count++;
    return p;
}

void *mt_calloc(size_t count, size_t size)
{
    void *p = calloc(count, size);
    if (p != NULL)
        live_count++;
    return p;
}

void *mt_realloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (p == NULL && q != NULL)
        live_count++;          /* realloc(NULL, n) is a new allocation */
    return q;
}

void mt_free(void *p)
{
    if (p != NULL)
        live_count--;
    free(p);
}

long mt_live(void)
{
    return live_count;
}
