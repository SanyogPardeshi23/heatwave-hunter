/*
 * list.c — Exp 2: singly linked list.
 * Used for the watchlist (city names) and for a cell's day-by-day history.
 * No malloc: nodes come from a fixed pool with a free list.
 */
#include "hh.h"

#define POOL_SIZE 512
#define CITY_LEN 24

static Node pool[POOL_SIZE];
static Node *freeHead = 0;
static int poolReady = 0;

static void poolInit(void) {
    int i;
    for (i = 0; i < POOL_SIZE - 1; i++) pool[i].next = &pool[i + 1];
    pool[POOL_SIZE - 1].next = 0;
    freeHead = &pool[0];
    poolReady = 1;
}

Node *poolAlloc(void) {
    Node *n;
    if (!poolReady) poolInit();
    if (!freeHead) { trace_line(2, "poolAlloc(): pool full"); return 0; }
    n = freeHead; freeHead = n->next; n->next = 0;
    return n;
}

void poolFree(Node *n) { if (n) { n->next = freeHead; freeHead = n; } }

void createSLL(Node **head) { *head = 0; }

void insertBegin(Node **head, const char *city, float value) {
    Node *n = poolAlloc();
    if (!n) return;
    strcopy(n->city, city, CITY_LEN);
    n->value = value;
    n->next = *head;
    *head = n;
    if (g_verbose) {
        Buf *t = trace_begin(2);
        buf_s(t, "insertBegin(&head, \""); buf_s(t, city); buf_s(t, "\")  node at ");
        buf_hex(t, (unsigned long)n);
        trace_end();
    }
}

int insertAfter(Node *head, const char *key, const char *city, float value) {
    Node *cur = head, *n;
    int steps = 0;
    while (cur && !streqi(cur->city, key)) { cur = cur->next; steps++; }
    if (!cur) {
        if (g_verbose) {
            Buf *t = trace_begin(2);
            buf_s(t, "insertAfter(head, \""); buf_s(t, key); buf_s(t, "\", \""); buf_s(t, city);
            buf_s(t, "\")  returns 0: key not in list");
            trace_end();
        }
        return 0;
    }
    n = poolAlloc();
    if (!n) return 0;
    strcopy(n->city, city, CITY_LEN);
    n->value = value;
    n->next = cur->next;
    cur->next = n;
    if (g_verbose) {
        Buf *t = trace_begin(2);
        buf_s(t, "insertAfter(head, \""); buf_s(t, cur->city); buf_s(t, "\", \""); buf_s(t, city);
        buf_s(t, "\")  walked "); buf_i(t, steps); buf_s(t, " nodes, new node at ");
        buf_hex(t, (unsigned long)n);
        trace_end();
    }
    return 1;
}

int deleteBefore(Node **head, const char *key) {
    Node *pp = 0, *prev = 0, *cur = *head;
    while (cur && !streqi(cur->city, key)) { pp = prev; prev = cur; cur = cur->next; }
    if (!cur || !prev) {                       /* key missing, or key is the head */
        if (g_verbose) {
            Buf *t = trace_begin(2);
            buf_s(t, "deleteBefore(&head, \""); buf_s(t, key); buf_s(t, "\")  returns 0: ");
            buf_s(t, !cur ? "key not in list" : "key is the head, nothing before it");
            trace_end();
        }
        return 0;
    }
    if (pp) pp->next = cur; else *head = cur;  /* unlink prev */
    if (g_verbose) {
        Buf *t = trace_begin(2);
        buf_s(t, "deleteBefore(&head, \""); buf_s(t, cur->city); buf_s(t, "\")  freed \"");
        buf_s(t, prev->city); buf_s(t, "\" at "); buf_hex(t, (unsigned long)prev);
        trace_end();
    }
    poolFree(prev);
    return 1;
}

int listLength(Node *head) { int n = 0; while (head) { n++; head = head->next; } return n; }

void display(Node *head, Buf *json) {
    Node *cur; int i = 0;
    buf_c(json, '[');
    for (cur = head; cur; cur = cur->next, i++) {
        if (i) buf_c(json, ',');
        buf_s(json, "{\"city\":"); j_str(json, cur->city);
        buf_s(json, ",\"value\":"); buf_f1(json, cur->value);
        buf_s(json, ",\"addr\":\""); buf_hex(json, (unsigned long)cur);
        buf_s(json, "\",\"next\":\"");
        if (cur->next) buf_hex(json, (unsigned long)cur->next); else buf_s(json, "NULL");
        buf_s(json, "\"}");
    }
    buf_c(json, ']');
    if (g_verbose) {
        Buf *t = trace_begin(2);
        buf_s(t, "display(head)  "); buf_i(t, i); buf_s(t, " rows");
        trace_end();
    }
}

void freeList(Node **head) {
    Node *cur = *head, *nx;
    while (cur) { nx = cur->next; poolFree(cur); cur = nx; }
    *head = 0;
}
