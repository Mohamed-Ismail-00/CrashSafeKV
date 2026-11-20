/*
 * hashtable.h - In-memory key -> value map (separate chaining, FNV-1a 64).
 *
 * Keys and values are arbitrary bytes (may contain '\0').
 * Ownership: the table stores its OWN copies of keys and values. Pointers it
 * returns stay valid only until the next put/remove/destroy on that key.
 */
#ifndef CSKV_HASHTABLE_H
#define CSKV_HASHTABLE_H

#include <stddef.h>

typedef struct ht ht;

/* Returns NULL when out of memory. */
ht *ht_create(void);

/* Frees the table and every key/value it owns. NULL is allowed. */
void ht_destroy(ht *t);

/* Insert or overwrite. Returns 0 on success, -1 when out of memory
 * (in which case the table is unchanged). */
int ht_put(ht *t, const void *key, size_t klen, const void *val, size_t vlen);

/* Pointer to the stored value (no copy) and its length, or NULL if missing.
 * An empty value still returns a non-NULL pointer with *out_vlen == 0. */
const void *ht_get(const ht *t, const void *key, size_t klen, size_t *out_vlen);

/* Returns 1 if the key was removed, 0 if it was not present. */
int ht_remove(ht *t, const void *key, size_t klen);

size_t ht_count(const ht *t);

/* Visits every entry in unspecified order. Stops when fn returns non-zero
 * and returns that value; returns 0 after a full pass. Do not modify the
 * table from inside fn. */
typedef int (*ht_visit_fn)(const void *key, size_t klen,
                           const void *val, size_t vlen, void *user);
int ht_foreach(const ht *t, ht_visit_fn fn, void *user);

#endif /* CSKV_HASHTABLE_H */
