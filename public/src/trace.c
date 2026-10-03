/*
 * trace.c — the live function log.
 * The log itself is a circular queue (counter method, like Exp 4):
 * new lines go in at rear; the page drains them from front. When the queue is
 * full the oldest line is dropped so the newest is never lost.
 */
#include "hh.h"

int  g_verbose = 0;
long g_cmp = 0;

typedef struct { int exp; char text[TRACE_LEN]; } TraceLine;

static TraceLine lines[TRACE_CAP];
static int t_front = 0, t_rear = -1, t_count = 0;

static char cur_mem[TRACE_LEN];
static Buf  cur;
static int  cur_exp;

void trace_line(int exp, const char *text) {
    if (t_count == TRACE_CAP) {                 /* full: drop oldest */
        t_front = (t_front + 1) % TRACE_CAP;
        t_count--;
    }
    t_rear = (t_rear + 1) % TRACE_CAP;
    lines[t_rear].exp = exp;
    strcopy(lines[t_rear].text, text, TRACE_LEN);
    t_count++;
}

Buf *trace_begin(int exp) {
    cur_exp = exp;
    buf_init(&cur, cur_mem, TRACE_LEN);
    return &cur;
}

void trace_end(void) { trace_line(cur_exp, cur_mem); }

int trace_drain_json(Buf *out) {
    int n = 0;
    buf_c(out, '[');
    while (t_count > 0) {
        TraceLine *l = &lines[t_front];
        if (n++) buf_c(out, ',');
        buf_s(out, "{\"e\":"); buf_i(out, l->exp);
        buf_s(out, ",\"t\":"); j_str(out, l->text);
        buf_c(out, '}');
        t_front = (t_front + 1) % TRACE_CAP;
        t_count--;
    }
    buf_c(out, ']');
    return n;
}
