/*
 * bst.c — Exp 5: binary search tree with two-pointer (left/right) nodes.
 * Key = max temperature, so "every cell at or above 45 °C" is a range search
 * that skips whole left subtrees.
 */
#include "hh.h"

#define BPOOL 1024
static BNode bpool[BPOOL];
static BNode *bfree = 0;
static int bready = 0;

static BNode *bAlloc(void) {
    BNode *n;
    if (!bready) {
        int i;
        for (i = 0; i < BPOOL - 1; i++) bpool[i].left = &bpool[i + 1];
        bpool[BPOOL - 1].left = 0; bfree = &bpool[0]; bready = 1;
    }
    if (!bfree) { trace_line(5, "bstInsert(): node pool full"); return 0; }
    n = bfree; bfree = n->left; n->left = n->right = 0;
    return n;
}
static void bRelease(BNode *n) { n->left = bfree; n->right = 0; bfree = n; }

int bstNodeId(BNode *n) { return n ? (int)(n - bpool) : -1; }

BNode *bstInsert(BNode *root, float key, int cell) {
    BNode *n, *cur, *parent = 0;
    n = bAlloc();
    if (!n) return root;
    n->key = key; n->cell = cell;
    if (!root) return n;
    {
        char path[64]; int pl = 0;
        cur = root;
        while (cur) {                       /* iterative: no deep recursion */
            parent = cur; g_cmp++;
            if (pl < 62) path[pl++] = (key < cur->key) ? 'L' : 'R';
            cur = (key < cur->key) ? cur->left : cur->right;   /* equal keys go right */
        }
        path[pl] = 0;
        if (key < parent->key) parent->left = n; else parent->right = n;
        if (g_verbose) {
            Buf *t = trace_begin(5);
            buf_s(t, "bstInsert(root, "); buf_f1(t, key); buf_s(t, ")  path "); buf_s(t, path);
            buf_s(t, "  depth "); buf_i(t, pl);
            trace_end();
        }
    }
    return root;
}

BNode *bstSearch(BNode *root, float key) {
    int steps = 0;
    while (root) {
        g_cmp++; steps++;
        if (g_verbose) {
            Buf *t = trace_begin(5);
            buf_s(t, "bstSearch: compare "); buf_f1(t, key); buf_s(t, " with node "); buf_f1(t, root->key);
            buf_s(t, key == root->key ? "  found" : key < root->key ? "  go left" : "  go right");
            trace_end();
        }
        if (key == root->key) return root;
        root = key < root->key ? root->left : root->right;
    }
    if (g_verbose) {
        Buf *t = trace_begin(5);
        buf_s(t, "bstSearch: "); buf_f1(t, key); buf_s(t, " not found after "); buf_i(t, steps); buf_s(t, " comparisons");
        trace_end();
    }
    return 0;
}

BNode *bstDelete(BNode *root, float key, int *deleted) {
    BNode *cur = root, *parent = 0, *child;
    while (cur && cur->key != key) { g_cmp++; parent = cur; cur = key < cur->key ? cur->left : cur->right; }
    if (!cur) { *deleted = 0; return root; }
    *deleted = 1;
    if (cur->left && cur->right) {          /* two children: copy in-order successor */
        BNode *sp = cur, *s = cur->right;
        while (s->left) { sp = s; s = s->left; }
        cur->key = s->key; cur->cell = s->cell;
        if (sp == cur) sp->right = s->right; else sp->left = s->right;
        bRelease(s);
        return root;
    }
    child = cur->left ? cur->left : cur->right;
    if (!parent) root = child;
    else if (parent->left == cur) parent->left = child;
    else parent->right = child;
    bRelease(cur);
    return root;
}

void bstFree(BNode *root) {             /* post-order: children before parent */
    if (!root) return;
    bstFree(root->left);
    bstFree(root->right);
    bRelease(root);
}

int bstHeight(BNode *r) {
    int a, b;
    if (!r) return 0;
    a = bstHeight(r->left); b = bstHeight(r->right);
    return 1 + (a > b ? a : b);
}

int bstCount(BNode *r) { return r ? 1 + bstCount(r->left) + bstCount(r->right) : 0; }

int inorder(BNode *r, BNode **out, int n) {
    if (!r) return n;
    n = inorder(r->left, out, n); out[n++] = r; return inorder(r->right, out, n);
}
int preorder(BNode *r, BNode **out, int n) {
    if (!r) return n;
    out[n++] = r; n = preorder(r->left, out, n); return preorder(r->right, out, n);
}
int postorder(BNode *r, BNode **out, int n) {
    if (!r) return n;
    n = postorder(r->left, out, n); n = postorder(r->right, out, n); out[n++] = r; return n;
}

int levelOrder(BNode *root, BNode **out, int max) {     /* breadth-first, uses Exp 4 queue */
    static CQueue q; int n = 0, id;
    if (!root) return 0;
    createQueue(&q, CQ_MAX);
    enqueue(&q, bstNodeId(root));
    while (dequeue(&q, &id) && n < max) {
        BNode *p = &bpool[id];
        out[n++] = p;
        if (p->left) enqueue(&q, bstNodeId(p->left));
        if (p->right) enqueue(&q, bstNodeId(p->right));
    }
    return n;
}

/* every node with key >= lo, in ascending order; counts nodes visited */
int rangeSearch(BNode *r, float lo, int *out, int n, int max, int *visited) {
    if (!r) return n;
    (*visited)++; g_cmp++;
    if (r->key >= lo) {
        n = rangeSearch(r->left, lo, out, n, max, visited);
        if (n < max) out[n++] = r->cell;
    }
    return rangeSearch(r->right, lo, out, n, max, visited);
}
