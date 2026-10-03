/*
 * graph.c — Exp 6: graph as an adjacency matrix + breadth-first search.
 * Main graph: vertices = land grid cells; an edge joins cells that touch
 * (N, S, E, W). A heatwave region = the cells one BFS reaches while staying
 * on hot cells. Lab mode uses a second, small graph with the same functions.
 */
#include "hh.h"

void graphInit(Graph *g, unsigned char *matrix, int n) {
    int i;
    g->m = matrix; g->n = n;
    for (i = 0; i < n * n; i++) matrix[i] = 0;
}

void addEdge(Graph *g, int u, int v) {
    if (u < 0 || v < 0 || u >= g->n || v >= g->n || u == v) return;
    g->m[u * g->n + v] = 1;
    g->m[v * g->n + u] = 1;
    if (g_verbose) {
        Buf *t = trace_begin(6);
        buf_s(t, "addEdge(g, "); buf_i(t, u); buf_s(t, ", "); buf_i(t, v);
        buf_s(t, ")  adj["); buf_i(t, u); buf_s(t, "]["); buf_i(t, v); buf_s(t, "] = adj[");
        buf_i(t, v); buf_s(t, "]["); buf_i(t, u); buf_s(t, "] = 1");
        trace_end();
    }
}

int hasEdge(Graph *g, int u, int v) { return g->m[u * g->n + v]; }

/*
 * BFS from start over vertices where allowed[v] != 0 (allowed may be 0 = all).
 * order[] gets the visit order, level[] the hop distance (-1 = not reached).
 * Returns how many vertices were reached; *checks = matrix entries examined.
 */
int bfs(Graph *g, int start, const unsigned char *allowed, int *order, int *level, int *checks) {
    static CQueue q;
    int v, w, n = 0, c = 0;
    for (v = 0; v < g->n; v++) level[v] = -1;
    if (start < 0 || start >= g->n || (allowed && !allowed[start])) { *checks = 0; return 0; }
    createQueue(&q, CQ_MAX);
    level[start] = 0;
    enqueue(&q, start);
    while (dequeue(&q, &v)) {
        order[n++] = v;
        for (w = 0; w < g->n; w++) {            /* scan row v of the matrix */
            c++;
            if (g->m[v * g->n + w] && level[w] < 0 && (!allowed || allowed[w])) {
                level[w] = level[v] + 1;
                enqueue(&q, w);
            }
        }
        if (g_verbose) {
            Buf *t = trace_begin(6);
            buf_s(t, "bfs: dequeue "); buf_i(t, v); buf_s(t, " (level "); buf_i(t, level[v]);
            buf_s(t, "), queue now holds "); buf_i(t, q.count);
            trace_end();
        }
    }
    *checks = c;
    return n;
}
