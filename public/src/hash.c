/*
 * hash.c — Exp 8: hash table (dictionary) on a circular array with
 * linear probing. Keys are city names; values are grid-cell indices.
 * Delete leaves a DELETED marker so later searches keep probing past it.
 */
#include "hh.h"

static void probeTrace(const char *fn, const char *key, int i, Slot *s);

void hashInit(HashTable *h, int cap) {
    int i;
    h->cap = (cap > 0 && cap <= HT_MAX) ? cap : HT_MAX;
    h->count = 0;
    for (i = 0; i < HT_MAX; i++) { h->slots[i].state = SLOT_EMPTY; h->slots[i].key[0] = 0; h->slots[i].value = -1; }
}

unsigned hashKey(const char *key, int cap) {      /* djb2 on the lower-cased key */
    unsigned h = 5381;
    while (*key) { h = h * 33u + (unsigned char)to_lower(*key); key++; }
    return h % (unsigned)cap;
}

int hashInsert(HashTable *h, const char *key, int value, int *slot, int *probes) {
    int i = (int)hashKey(key, h->cap), p = 0, firstFree = -1;
    while (p < h->cap) {                            /* one full cycle at most */
        Slot *s = &h->slots[i];
        p++;
        probeTrace("insert", key, i, s);
        if (s->state == SLOT_USED && streqi(s->key, key)) { s->value = value; *slot = i; *probes = p; return 1; }
        if (s->state != SLOT_USED && firstFree < 0) firstFree = i;
        if (s->state == SLOT_EMPTY) break;
        i = (i + 1) % h->cap;
    }
    *probes = p;
    if (firstFree < 0) { *slot = -1; return 0; }    /* table full */
    strcopy(h->slots[firstFree].key, key, 24);
    h->slots[firstFree].value = value;
    h->slots[firstFree].state = SLOT_USED;
    h->count++;
    *slot = firstFree;
    return 1;
}

static void probeTrace(const char *fn, const char *key, int i, Slot *s) {
    Buf *t;
    if (!g_verbose) return;
    t = trace_begin(8);
    buf_s(t, fn); buf_s(t, "(\""); buf_s(t, key); buf_s(t, "\"): probe slot ["); buf_i(t, i); buf_s(t, "] ");
    buf_s(t, s->state == SLOT_EMPTY ? "empty" : s->state == SLOT_DELETED ? "deleted marker" : s->key);
    trace_end();
}

int hashSearch(HashTable *h, const char *key, int *slot, int *probes) {
    int i = (int)hashKey(key, h->cap), p = 0;
    while (p < h->cap) {
        Slot *s = &h->slots[i];
        p++; g_cmp++;
        probeTrace("search", key, i, s);
        if (s->state == SLOT_EMPTY) break;
        if (s->state == SLOT_USED && streqi(s->key, key)) { *slot = i; *probes = p; return s->value; }
        i = (i + 1) % h->cap;
    }
    *slot = -1; *probes = p;
    return -1;
}

int hashDelete(HashTable *h, const char *key, int *slot, int *probes) {
    int v = hashSearch(h, key, slot, probes);
    if (v < 0) return 0;
    h->slots[*slot].state = SLOT_DELETED;
    h->count--;
    return 1;
}
