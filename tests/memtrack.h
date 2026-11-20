/*
 * memtrack.h - Allocation counter used by `make leakcheck`.
 *
 * The leak-check build force-includes this header into every engine and test
 * source (gcc -include), so malloc/calloc/realloc/free go through counting
 * wrappers. After each test the number of live allocations must be zero,
 * which catches leaks and double frees in our own code.
 *
 * It exists because Dr. Memory cannot run on every machine (see
 * KNOWN_ISSUES.md); `make memcheck` still runs Dr. Memory where it works.
 */
#ifndef CSKV_MEMTRACK_H
#define CSKV_MEMTRACK_H

#ifdef CSKV_MEMTRACK
#include <stddef.h>
#include <stdlib.h>   /* real prototypes first, then the redirecting macros */

void *mt_malloc(size_t n);
void *mt_calloc(size_t count, size_t size);
void *mt_realloc(void *p, size_t n);
void  mt_free(void *p);
long  mt_live(void);  /* allocations not yet freed */

#define malloc(n)      mt_malloc(n)
#define calloc(c, s)   mt_calloc((c), (s))
#define realloc(p, n)  mt_realloc((p), (n))
#define free(p)        mt_free(p)
#endif

#endif /* CSKV_MEMTRACK_H */
