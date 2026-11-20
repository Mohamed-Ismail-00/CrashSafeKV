/*
 * hashtable.c - Separate-chaining hash table with FNV-1a 64-bit hashing.
 *
 * Each bucket holds a singly linked list of entries. The full 64-bit hash is
 * kept in every entry so resizing never has to rehash the key bytes, and
 * lookups compare hashes before doing a memcmp.
 */
#include "hashtable.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HT_INITIAL_CAPACITY 64u

typedef struct ht_entry {
    struct ht_entry *next;
    uint64_t hash;
    unsigned char *key;
    unsigned char *val;
    size_t klen;
    size_t vlen;
} ht_entry;

struct ht {
    ht_entry **buckets;
    size_t capacity;   /* always a power of two */
    size_t count;
};

static uint64_t fnv1a64(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint64_t h = 14695981039346656037ull;   /* FNV offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ull;               /* FNV prime */
    }
    return h;
}

/* malloc(0) may return NULL, so always allocate at least one byte. */
static unsigned char *dup_bytes(const void *src, size_t len)
{
    unsigned char *p = malloc(len ? len : 1);
    if (p != NULL && len > 0)
        memcpy(p, src, len);
    return p;
}

static size_t bucket_of(const ht *t, uint64_t hash)
{
    return (size_t)(hash & (uint64_t)(t->capacity - 1));
}

/* Returns the address of the link that points at the matching entry
 * (or at the NULL that ends the chain). Lets put/remove share one search. */
static ht_entry **find_link(const ht *t, const void *key, size_t klen,
                            uint64_t hash)
{
    ht_entry **link = &t->buckets[bucket_of(t, hash)];
    while (*link != NULL) {
        ht_entry *e = *link;
        if (e->hash == hash && e->klen == klen &&
            (klen == 0 || memcmp(e->key, key, klen) == 0))
            return link;
        link = &e->next;
    }
    return link;
}

ht *ht_create(void)
{
    ht *t = malloc(sizeof *t);
    if (t == NULL)
        return NULL;
    t->buckets = calloc(HT_INITIAL_CAPACITY, sizeof *t->buckets);
    if (t->buckets == NULL) {
        free(t);
        return NULL;
    }
    t->capacity = HT_INITIAL_CAPACITY;
    t->count = 0;
    return t;
}

void ht_destroy(ht *t)
{
    if (t == NULL)
        return;
    for (size_t i = 0; i < t->capacity; i++) {
        ht_entry *e = t->buckets[i];
        while (e != NULL) {
            ht_entry *next = e->next;
            free(e->key);
            free(e->val);
            free(e);
            e = next;
        }
    }
    free(t->buckets);
    free(t);
}

/* Double the bucket array and move every entry. On allocation failure the
 * table keeps working at its current size (just with longer chains). */
static void grow(ht *t)
{
    size_t new_cap = t->capacity * 2;
    ht_entry **nb = calloc(new_cap, sizeof *nb);
    if (nb == NULL)
        return;
    for (size_t i = 0; i < t->capacity; i++) {
        ht_entry *e = t->buckets[i];
        while (e != NULL) {
            ht_entry *next = e->next;
            size_t b = (size_t)(e->hash & (uint64_t)(new_cap - 1));
            e->next = nb[b];
            nb[b] = e;
            e = next;
        }
    }
    free(t->buckets);
    t->buckets = nb;
    t->capacity = new_cap;
}

int ht_put(ht *t, const void *key, size_t klen, const void *val, size_t vlen)
{
    uint64_t hash = fnv1a64(key, klen);
    ht_entry **link = find_link(t, key, klen, hash);

    /* Copy the new value first: if that fails, nothing has changed yet. */
    unsigned char *vcopy = dup_bytes(val, vlen);
    if (vcopy == NULL)
        return -1;

    if (*link != NULL) {                 /* overwrite existing key */
        free((*link)->val);
        (*link)->val = vcopy;
        (*link)->vlen = vlen;
        return 0;
    }

    ht_entry *e = malloc(sizeof *e);
    unsigned char *kcopy = dup_bytes(key, klen);
    if (e == NULL || kcopy == NULL) {
        free(e);
        free(kcopy);
        free(vcopy);
        return -1;
    }
    e->hash = hash;
    e->key = kcopy;
    e->klen = klen;
    e->val = vcopy;
    e->vlen = vlen;
    e->next = *link;                     /* *link is NULL: append to chain */
    *link = e;
    t->count++;

    /* Keep the load factor at or below 0.75. */
    if (t->count * 4 > t->capacity * 3)
        grow(t);
    return 0;
}

const void *ht_get(const ht *t, const void *key, size_t klen, size_t *out_vlen)
{
    ht_entry *e = *find_link(t, key, klen, fnv1a64(key, klen));
    if (e == NULL)
        return NULL;
    if (out_vlen != NULL)
        *out_vlen = e->vlen;
    return e->val;
}

int ht_remove(ht *t, const void *key, size_t klen)
{
    ht_entry **link = find_link(t, key, klen, fnv1a64(key, klen));
    ht_entry *e = *link;
    if (e == NULL)
        return 0;
    *link = e->next;                     /* unlink, then free */
    free(e->key);
    free(e->val);
    free(e);
    t->count--;
    return 1;
}

size_t ht_count(const ht *t)
{
    return t->count;
}

int ht_foreach(const ht *t, ht_visit_fn fn, void *user)
{
    for (size_t i = 0; i < t->capacity; i++) {
        for (ht_entry *e = t->buckets[i]; e != NULL; e = e->next) {
            int rc = fn(e->key, e->klen, e->val, e->vlen, user);
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}
