/*
 * queue.c — Exp 4: static circular queue using the counter method.
 * front, rear and count; insert at rear, delete from front, indices wrap.
 * Used for the incoming-day feed and as the BFS queue in graph.c.
 */
#include "hh.h"

void createQueue(CQueue *q, int cap) {
    q->cap = (cap > 0 && cap <= CQ_MAX) ? cap : CQ_MAX;
    q->front = 0;
    q->rear = -1;
    q->count = 0;
}

int isFull(CQueue *q)   { return q->count == q->cap; }
int isQEmpty(CQueue *q) { return q->count == 0; }

int enqueue(CQueue *q, int v) {
    if (isFull(q)) return 0;
    q->rear = (q->rear + 1) % q->cap;
    q->items[q->rear] = v;
    q->count++;
    return 1;
}

int dequeue(CQueue *q, int *v) {
    if (isQEmpty(q)) return 0;
    if (v) *v = q->items[q->front];
    q->front = (q->front + 1) % q->cap;
    q->count--;
    return 1;
}

int queueFront(CQueue *q) { return isQEmpty(q) ? -1 : q->items[q->front]; }
int queueRear(CQueue *q)  { return isQEmpty(q) ? -1 : q->items[q->rear]; }
