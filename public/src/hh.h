/*
 * hh.h — Heatwave Hunter core (all logic lives in C; the web page only draws).
 * Compiled with -nostdlib: no printf, no malloc, no string.h. Everything below
 * is written by hand, in the style of the DS lab experiments.
 */
#ifndef HH_H
#define HH_H

typedef unsigned long hsize;

/* ---------- mem.c : memory + string helpers (no libc) ---------- */
void *memset(void *d, int c, hsize n);
void *memcpy(void *d, const void *s, hsize n);
void *memmove(void *d, const void *s, hsize n);
int   str_len(const char *s);
int   streq(const char *a, const char *b);          /* exact */
int   streqi(const char *a, const char *b);         /* case-insensitive */
void  strcopy(char *dst, const char *src, int cap); /* always terminates */
char  to_lower(char c);
int   is_digit(char c);
int   is_alpha(char c);
int   is_space(char c);
float parse_float(const char *s, int *ok);
int   parse_int(const char *s, int *ok);

/* tiny text builder used by the trace log and JSON writer */
typedef struct { char *buf; int cap; int len; } Buf;
void buf_init(Buf *b, char *mem, int cap);
void buf_s(Buf *b, const char *s);
void buf_c(Buf *b, char c);
void buf_i(Buf *b, long v);
void buf_f1(Buf *b, float v);   /* one decimal */
void buf_hex(Buf *b, unsigned long v);

/* ---------- trace.c : live function log (a circular queue of lines) ---------- */
#define TRACE_CAP   256
#define TRACE_LEN   150
extern int g_verbose;            /* 1 = log every call (Lab), 0 = summaries only */
extern long g_cmp;               /* comparison counter, reset by callers */
void  trace_line(int exp, const char *text);
Buf  *trace_begin(int exp);      /* start a line; returns builder */
void  trace_end(void);
int   trace_drain_json(Buf *out); /* writes [{"e":..,"t":".."},...] and empties */

/* ---------- json helpers ---------- */
void j_str(Buf *b, const char *s);              /* quoted + escaped */
void j_key(Buf *b, const char *k);              /* "k": */

/* ---------- grid.c : Exp 1 — array of structures ---------- */
#define GN 31
#define MAX_YEARS 16
#define SEASON_DAYS 122
#define MAX_CELLS 400
#define NO_DATA (-32768)

typedef struct Cell {
    int   row, col;       /* grid indices */
    float lat, lon;
    float tmax;           /* °C for the loaded day */
    int   valid;          /* 0 = IMD had no reading */
    int   index;          /* 0..ncells-1, or -1 for sea / outside India */
} Cell;

extern Cell  grid[GN][GN];
extern int   g_nyears, g_ndays, g_ncells;
extern int   g_years[MAX_YEARS];
extern int   g_cellRow[MAX_CELLS], g_cellCol[MAX_CELLS];
extern int   g_cellAt[GN][GN];

unsigned char *data_buffer(void);
int   data_capacity(void);
int   data_parse(int len);                     /* returns 1 if ok */
float cell_value(int yi, int d, int cell, int *valid);
void  readGrid(int yi, int d);                 /* fills grid[][] */
Cell *getCell(float lat, float lon);
int   updateCell(Cell *c, float tmax);
Cell *cellByIndex(int idx);

/* ---------- list.c : Exp 2 — singly linked list ---------- */
typedef struct Node {
    char  city[24];
    float value;
    struct Node *next;
} Node;
Node *poolAlloc(void);
void  poolFree(Node *n);
void  createSLL(Node **head);
void  insertBegin(Node **head, const char *city, float value);
int   insertAfter(Node *head, const char *key, const char *city, float value);
int   deleteBefore(Node **head, const char *key);
int   listLength(Node *head);
void  display(Node *head, Buf *json);          /* writes JSON rows */
void  freeList(Node **head);

/* ---------- stack.c : Exp 3 — stack on a linked list, infix -> postfix ---------- */
#define TOK_NUM 1
#define TOK_VAR 2
#define TOK_OP  3
#define TOK_LP  4
#define TOK_RP  5
#define MAX_TOK 64
typedef struct { int type; float num; char text[8]; } Token;
typedef struct SNode { Token tok; float val; struct SNode *next; } SNode;
typedef struct { SNode *top; int size; } Stack;
void  createStack(Stack *s);
void  push(Stack *s, Token t);
Token pop(Stack *s);
Token peek(Stack *s);
int   isEmpty(Stack *s);
void  pushVal(Stack *s, float v);
float popVal(Stack *s);
void  clearStack(Stack *s);
int   tokenize(const char *src, Token *out, int max, char *err);
int   toPostfix(Token *in, int n, Token *out, Buf *steps); /* steps may be 0 */
float evalPostfix(Token *pf, int n, float T, float days, float lat, float lon, int *ok);
void  postfixText(Token *pf, int n, Buf *b);

/* ---------- queue.c : Exp 4 — static circular queue, counter method ---------- */
#define CQ_MAX 512
typedef struct { int items[CQ_MAX]; int front, rear, count, cap; } CQueue;
void createQueue(CQueue *q, int cap);
int  isFull(CQueue *q);
int  isQEmpty(CQueue *q);
int  enqueue(CQueue *q, int v);
int  dequeue(CQueue *q, int *v);
int  queueFront(CQueue *q);
int  queueRear(CQueue *q);

/* ---------- bst.c : Exp 5 — binary search tree (two-pointer nodes) ---------- */
typedef struct BNode { float key; int cell; struct BNode *left, *right; } BNode;
BNode *bstInsert(BNode *root, float key, int cell);
BNode *bstSearch(BNode *root, float key);
BNode *bstDelete(BNode *root, float key, int *deleted);
void   bstFree(BNode *root);
int    bstHeight(BNode *root);
int    bstCount(BNode *root);
int    inorder(BNode *root, BNode **out, int n);
int    preorder(BNode *root, BNode **out, int n);
int    postorder(BNode *root, BNode **out, int n);
int    levelOrder(BNode *root, BNode **out, int max);
int    rangeSearch(BNode *root, float lo, int *outCells, int n, int max, int *visited);
int    bstNodeId(BNode *n);

/* ---------- graph.c : Exp 6 — adjacency matrix + BFS ---------- */
#define GV MAX_CELLS
typedef struct { unsigned char *m; int n; } Graph;   /* m = n x n adjacency matrix */
void graphInit(Graph *g, unsigned char *matrix, int n);
void addEdge(Graph *g, int u, int v);
int  hasEdge(Graph *g, int u, int v);
int  bfs(Graph *g, int start, const unsigned char *allowed, int *order, int *level, int *checks);

/* ---------- sort.c : Exp 7 — sorting + binary search ---------- */
typedef struct { float key; int id; } Item;
void quickSort(Item *a, int lo, int hi);          /* ascending */
void mergeSort(Item *a, int n);                   /* ascending, stable */
void insertionSort(Item *a, int n);               /* baseline for comparison */
int  binarySearch(Item *a, int n, float key, int *steps); /* first index with key >= target */
void reverseItems(Item *a, int n);

/* ---------- hash.c : Exp 8 — hash table, circular array, linear probing ---------- */
#define HT_MAX 128
#define SLOT_EMPTY 0
#define SLOT_USED 1
#define SLOT_DELETED 2
typedef struct { char key[24]; int value; int state; } Slot;
typedef struct { Slot slots[HT_MAX]; int cap; int count; } HashTable;
void hashInit(HashTable *h, int cap);
unsigned hashKey(const char *key, int cap);
int  hashInsert(HashTable *h, const char *key, int value, int *slot, int *probes);
int  hashSearch(HashTable *h, const char *key, int *slot, int *probes); /* value or -1 */
int  hashDelete(HashTable *h, const char *key, int *slot, int *probes);

/* ---------- cities.c ---------- */
typedef struct { const char *name; float lat, lon; } City;
extern const City CITIES[];
extern const int NCITIES;

#endif
