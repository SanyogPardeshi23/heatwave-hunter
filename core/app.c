/*
 * app.c — glues the eight data structures into Heatwave Hunter and exposes
 * the functions the web page calls. Every answer is written as JSON text into
 * OUT; the page only parses and draws it.
 */
#include "hh.h"

#define MAXC MAX_CELLS
#define FEED_CAP 8
#define HIST_DAYS 14

/* ---------- I/O buffers shared with the page ---------- */
static char IN[8192];
static char OUTMEM[262144];
static Buf O;
char *hh_in(void)  { return IN; }
char *hh_out(void) { return OUTMEM; }
static void ob(void) { buf_init(&O, OUTMEM, sizeof OUTMEM); }

/* ---------- state ---------- */
static Graph G;
static unsigned char Gm[GV * GV];
static int yi = 0, day = 0, loaded = 0;
static float thr = 45.0f;
static BNode *dayTree = 0;
static CQueue feed;
static Node *watch = 0, *history = 0;
static HashTable cityTable;
static int cityCell[96];
static int cellCity[MAXC];
static unsigned char hot[MAXC];
static int regionOf[MAXC], nReg = 0, regOrderIdx[MAXC], regRank[MAXC];
static int regSize[MAXC], regTop[MAXC], regStart[MAXC];
static float regPeak[MAXC], regLat[MAXC], regLon[MAXC];
static int regChecks = 0;
static Token rulePf[MAX_TOK];
static int ruleN = 0;
static char ruleSrc[160] = "T >= 45 && days >= 2";
static unsigned char ruleHit[MAXC];
static int ruleCount = 0, rangeVisited = 0, rangeHits = 0, nValid = 0;
static int selCell = -1;
static int order[MAXC], level[MAXC];

static const char *MON[4] = {"Mar", "Apr", "May", "Jun"};
static const int MLEN[4] = {31, 30, 31, 30};

static void dayToMD(int d, int *m, int *md) {
    int k = 0;
    d++;
    while (k < 3 && d > MLEN[k]) { d -= MLEN[k]; k++; }
    *m = k; *md = d;
}
static void dateLabel(Buf *b, int d) { int m, md; dayToMD(d, &m, &md); buf_i(b, md); buf_c(b, ' '); buf_s(b, MON[m]); }
static int isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static float cellLat(int i) { return 7.5f + (float)g_cellRow[i]; }
static float cellLon(int i) { return 67.5f + (float)g_cellCol[i]; }

static int nearestCity(float lat, float lon, float maxd2) {
    int i, best = -1; float bd = maxd2;
    for (i = 0; i < NCITIES; i++) {
        float dl = CITIES[i].lat - lat, dn = CITIES[i].lon - lon, d2 = dl * dl + dn * dn;
        if (d2 < bd) { bd = d2; best = i; }
    }
    return best;
}

static int nearestLandCell(float lat, float lon) {
    int i, best = -1; float bd = 1e9f;
    for (i = 0; i < g_ncells; i++) {
        float dl = cellLat(i) - lat, dn = cellLon(i) - lon, d2 = dl * dl + dn * dn;
        if (d2 < bd) { bd = d2; best = i; }
    }
    return best;
}

/* k-th cell in a mixed order: k*97 mod n (97 is prime and doesn't divide n, so every cell appears once) */
static int insertOrder(int k) {
    long step = (g_ncells % 97 == 0) ? 89L : 97L;   /* both prime: coprime with n unless n is a multiple */
    return (int)(((long)k * step) % g_ncells);
}

static float todayT(int cell, int *valid) { return cell_value(yi, day, cell, valid); }

static int streak(int cell, int d) {
    int n = 0, v;
    while (d >= 0) {
        float t = cell_value(yi, d, cell, &v);
        if (!v || t < thr) break;
        n++; d--;
    }
    return n;
}

static void cityName(Buf *b, int cell) {
    if (cell >= 0 && cellCity[cell] >= 0) buf_s(b, CITIES[cellCity[cell]].name);
    else { buf_f1(b, cellLat(cell)); buf_s(b, "N "); buf_f1(b, cellLon(cell)); buf_s(b, "E"); }
}

/* ======================= start-up ======================= */
unsigned char *hh_data(void) { return data_buffer(); }
int hh_data_cap(void) { return data_capacity(); }

int hh_init(int len) {
    int i, r, c, slot, probes, edges = 0, maxProbe = 0;
    ob();
    if (!data_parse(len)) { buf_s(&O, "{\"ok\":0}"); return O.len; }

    graphInit(&G, Gm, g_ncells);
    for (i = 0; i < g_ncells; i++) {
        r = g_cellRow[i]; c = g_cellCol[i];
        if (r + 1 < GN && g_cellAt[r + 1][c] >= 0) { addEdge(&G, i, g_cellAt[r + 1][c]); edges++; }
        if (c + 1 < GN && g_cellAt[r][c + 1] >= 0) { addEdge(&G, i, g_cellAt[r][c + 1]); edges++; }
    }
    {
        Buf *t = trace_begin(6);
        buf_s(t, "graphInit + addEdge x"); buf_i(t, edges); buf_s(t, "  adjacency matrix "); buf_i(t, g_ncells);
        buf_s(t, " x "); buf_i(t, g_ncells);
        trace_end();
    }

    hashInit(&cityTable, 127);
    for (i = 0; i < NCITIES; i++) {
        cityCell[i] = nearestLandCell(CITIES[i].lat, CITIES[i].lon);
        hashInsert(&cityTable, CITIES[i].name, i, &slot, &probes);
        if (probes > maxProbe) maxProbe = probes;
    }
    for (i = 0; i < g_ncells; i++) cellCity[i] = nearestCity(cellLat(i), cellLon(i), 0.81f);
    {
        Buf *t = trace_begin(8);
        buf_s(t, "hashInsert x"); buf_i(t, NCITIES); buf_s(t, " cities into table[127]  longest probe "); buf_i(t, maxProbe);
        trace_end();
    }

    createQueue(&feed, FEED_CAP);
    createSLL(&watch);
    insertBegin(&watch, "Nagpur", 0); insertBegin(&watch, "Lucknow", 0);
    insertBegin(&watch, "Jaipur", 0); insertBegin(&watch, "Delhi", 0);
    trace_line(2, "createSLL(&watch) + insertBegin x4  (Delhi, Jaipur, Lucknow, Nagpur)");

    {
        Token tk[MAX_TOK]; char err[64]; int n = tokenize(ruleSrc, tk, MAX_TOK, err);
        ruleN = n > 0 ? toPostfix(tk, n, rulePf, 0) : 0;
        if (ruleN < 0) ruleN = 0;
    }

    buf_s(&O, "{\"ok\":1,\"years\":[");
    for (i = 0; i < g_nyears; i++) { if (i) buf_c(&O, ','); buf_i(&O, g_years[i]); }
    buf_s(&O, "],\"days\":"); buf_i(&O, g_ndays);
    buf_s(&O, ",\"cells\":[");
    for (i = 0; i < g_ncells; i++) {
        if (i) buf_c(&O, ',');
        buf_c(&O, '['); buf_i(&O, g_cellRow[i]); buf_c(&O, ','); buf_i(&O, g_cellCol[i]); buf_c(&O, ']');
    }
    buf_s(&O, "],\"cities\":[");
    for (i = 0; i < NCITIES; i++) {
        if (i) buf_c(&O, ',');
        buf_s(&O, "{\"name\":"); j_str(&O, CITIES[i].name);
        buf_s(&O, ",\"cell\":"); buf_i(&O, cityCell[i]); buf_c(&O, '}');
    }
    buf_s(&O, "],\"cellSize\":"); buf_i(&O, (long)sizeof(Cell));
    buf_s(&O, ",\"rule\":"); j_str(&O, ruleSrc);
    buf_s(&O, ",\"edges\":"); buf_i(&O, edges);
    buf_c(&O, '}');
    return O.len;
}

/* ======================= one day ======================= */
static void loadDay(int y, int d) {
    int i, changed = !loaded || y != yi || d != day;
    yi = y; day = d; loaded = 1;
    readGrid(yi, day);

    if (changed) {
        int old;
        if (isFull(&feed)) {
            dequeue(&feed, &old);
            Buf *t = trace_begin(4);
            buf_s(t, "dequeue(&feed) -> "); buf_i(t, g_years[old / 1000]); buf_c(t, ' '); dateLabel(t, old % 1000);
            buf_s(t, " (queue was full)");
            trace_end();
        }
        enqueue(&feed, yi * 1000 + day);
        {
            Buf *t = trace_begin(4);
            buf_s(t, "enqueue(&feed, "); buf_i(t, g_years[yi]); buf_c(t, ' '); dateLabel(t, day);
            buf_s(t, ")  front="); buf_i(t, feed.front); buf_s(t, " rear="); buf_i(t, feed.rear);
            buf_s(t, " count="); buf_i(t, feed.count); buf_c(t, '/'); buf_i(t, feed.cap);
            trace_end();
        }
    }

    /*
     * Insert in a spread-out order: grid order runs south to north and
     * temperature follows latitude, which would make a lopsided tree.
     * Stepping by a prime (97) visits every cell once in a mixed order.
     */
    bstFree(dayTree); dayTree = 0;
    g_cmp = 0; nValid = 0;
    for (i = 0; i < g_ncells; i++) {
        int ci = insertOrder(i);
        Cell *p = cellByIndex(ci);
        if (p->valid) { dayTree = bstInsert(dayTree, p->tmax, ci); nValid++; }
    }
    {
        Buf *t = trace_begin(5);
        buf_s(t, "bstInsert x"); buf_i(t, nValid); buf_s(t, " (mixed order)  "); buf_i(t, g_cmp);
        buf_s(t, " comparisons, height "); buf_i(t, bstHeight(dayTree));
        trace_end();
    }
}

static void evalRule(void) {
    int i, ok;
    ruleCount = 0;
    for (i = 0; i < g_ncells; i++) {
        Cell *p = cellByIndex(i);
        ruleHit[i] = 0;
        if (!p->valid || ruleN <= 0) continue;
        if (evalPostfix(rulePf, ruleN, p->tmax, (float)streak(i, day), p->lat, p->lon, &ok) != 0 && ok) {
            ruleHit[i] = 1; ruleCount++;
        }
    }
    {
        Buf *t = trace_begin(3);
        buf_s(t, "evalPostfix(\""); postfixText(rulePf, ruleN, t); buf_s(t, "\") x"); buf_i(t, nValid);
        buf_s(t, " cells -> "); buf_i(t, ruleCount); buf_s(t, " match");
        trace_end();
    }
}

static void analyze(void) {
    int i, k;
    static int tmpCells[MAXC];
    static Item items[MAXC];

    for (i = 0; i < g_ncells; i++) {
        Cell *p = cellByIndex(i);
        hot[i] = (unsigned char)(p->valid && p->tmax >= thr);
        regionOf[i] = -1;
    }
    nReg = 0; regChecks = 0;
    for (i = 0; i < g_ncells; i++) {
        if (hot[i] && regionOf[i] < 0) {
            int checks, n = bfs(&G, i, hot, order, level, &checks);
            float pk = -99, sl = 0, sn = 0; int top = i;
            regChecks += checks;
            for (k = 0; k < n; k++) {
                int v = order[k]; Cell *p = cellByIndex(v);
                regionOf[v] = nReg;
                if (p->tmax > pk) pk = p->tmax;
                sl += p->lat; sn += p->lon;
                if (g_cellRow[v] > g_cellRow[top] ||
                    (g_cellRow[v] == g_cellRow[top] && g_cellCol[v] < g_cellCol[top])) top = v;
            }
            regSize[nReg] = n; regPeak[nReg] = pk; regTop[nReg] = top; regStart[nReg] = i;
            regLat[nReg] = sl / (float)n; regLon[nReg] = sn / (float)n;
            nReg++;
        }
    }
    {
        Buf *t = trace_begin(6);
        buf_s(t, "bfs(adj, hot cells) x"); buf_i(t, nReg); buf_s(t, " -> "); buf_i(t, nReg);
        buf_s(t, " regions, "); buf_i(t, regChecks); buf_s(t, " matrix checks");
        trace_end();
    }

    for (k = 0; k < nReg; k++) { items[k].key = (float)regSize[k] * 100.0f + regPeak[k]; items[k].id = k; }
    g_cmp = 0;
    if (nReg > 1) quickSort(items, 0, nReg - 1);
    reverseItems(items, nReg);
    for (k = 0; k < nReg; k++) { regOrderIdx[k] = items[k].id; regRank[items[k].id] = k; }
    if (nReg) {
        Buf *t = trace_begin(7);
        buf_s(t, "quickSort(regions, "); buf_i(t, nReg); buf_s(t, ") by size  "); buf_i(t, g_cmp); buf_s(t, " comparisons");
        trace_end();
    }

    evalRule();

    rangeVisited = 0; g_cmp = 0;
    rangeHits = rangeSearch(dayTree, thr, tmpCells, 0, MAXC, &rangeVisited);
    {
        Buf *t = trace_begin(5);
        buf_s(t, "rangeSearch(root, "); buf_f1(t, thr); buf_s(t, ") -> "); buf_i(t, rangeHits);
        buf_s(t, " cells, visited "); buf_i(t, rangeVisited); buf_s(t, " of "); buf_i(t, nValid); buf_s(t, " nodes");
        trace_end();
    }
}

static void refreshWatchValues(void) {
    Node *p; int n = 0, slot, probes, total = 0;
    g_cmp = 0;
    for (p = watch; p; p = p->next) {
        int ci = hashSearch(&cityTable, p->city, &slot, &probes), v;
        total += probes; n++;
        p->value = -99;
        if (ci >= 0) { float t = todayT(cityCell[ci], &v); if (v) p->value = t; }
    }
    {
        Buf *t = trace_begin(8);
        buf_s(t, "hashSearch(cityTable, watchlist) x"); buf_i(t, n); buf_s(t, "  "); buf_i(t, total); buf_s(t, " probes");
        trace_end();
    }
}

static void writeWatch(void) {
    int saved = g_verbose; g_verbose = 0;
    display(watch, &O);
    g_verbose = saved;
}

static void writeDay(void) {
    int i, m, md, k;
    dayToMD(day, &m, &md);
    buf_s(&O, "{\"year\":"); buf_i(&O, g_years[yi]);
    buf_s(&O, ",\"yi\":"); buf_i(&O, yi);
    buf_s(&O, ",\"day\":"); buf_i(&O, day);
    buf_s(&O, ",\"date\":\""); dateLabel(&O, day); buf_c(&O, ' '); buf_i(&O, g_years[yi]); buf_c(&O, '"');
    buf_s(&O, ",\"doy\":"); buf_i(&O, (isLeap(g_years[yi]) ? 60 : 59) + day + 1);
    buf_s(&O, ",\"thr\":"); buf_f1(&O, thr);
    buf_s(&O, ",\"t\":[");
    for (i = 0; i < g_ncells; i++) {
        Cell *p = cellByIndex(i);
        if (i) buf_c(&O, ',');
        if (p->valid) buf_i(&O, (long)(p->tmax * 10.0f + (p->tmax >= 0 ? 0.5f : -0.5f))); else buf_s(&O, "null");
    }
    buf_s(&O, "],\"reg\":[");
    for (i = 0; i < g_ncells; i++) { if (i) buf_c(&O, ','); buf_i(&O, regionOf[i] < 0 ? -1 : regRank[regionOf[i]]); }
    buf_s(&O, "],\"rule\":\"");
    for (i = 0; i < g_ncells; i++) buf_c(&O, ruleHit[i] ? '1' : '0');
    buf_s(&O, "\",\"regions\":[");
    for (k = 0; k < nReg; k++) {
        int r = regOrderIdx[k], nc = nearestCity(regLat[r], regLon[r], 1e9f);
        if (k) buf_c(&O, ',');
        buf_s(&O, "{\"id\":\"HW-"); if (k < 9) buf_c(&O, '0'); buf_i(&O, k + 1);
        buf_s(&O, "\",\"size\":"); buf_i(&O, regSize[r]);
        buf_s(&O, ",\"peak\":"); buf_f1(&O, regPeak[r]);
        buf_s(&O, ",\"near\":"); j_str(&O, nc >= 0 ? CITIES[nc].name : "-");
        buf_s(&O, ",\"tag\":"); buf_i(&O, regTop[r]);
        buf_s(&O, ",\"start\":"); buf_i(&O, regStart[r]);
        buf_c(&O, '}');
    }
    buf_s(&O, "],\"feed\":{\"cap\":"); buf_i(&O, feed.cap);
    buf_s(&O, ",\"front\":"); buf_i(&O, feed.front);
    buf_s(&O, ",\"rear\":"); buf_i(&O, feed.rear);
    buf_s(&O, ",\"count\":"); buf_i(&O, feed.count);
    buf_s(&O, ",\"slots\":[");
    for (i = 0; i < feed.cap; i++) {
        int occupied = 0, j;
        for (j = 0; j < feed.count; j++) if ((feed.front + j) % feed.cap == i) occupied = 1;
        if (i) buf_c(&O, ',');
        if (occupied) {
            int v = feed.items[i];
            buf_s(&O, "{\"label\":\""); dateLabel(&O, v % 1000); buf_s(&O, "\",\"year\":"); buf_i(&O, g_years[v / 1000]); buf_c(&O, '}');
        } else buf_s(&O, "null");
    }
    buf_s(&O, "]},\"watch\":"); writeWatch();
    buf_s(&O, ",\"stats\":{\"valid\":"); buf_i(&O, nValid);
    buf_s(&O, ",\"hot\":"); buf_i(&O, rangeHits);
    buf_s(&O, ",\"treeH\":"); buf_i(&O, bstHeight(dayTree));
    buf_s(&O, ",\"rangeVisited\":"); buf_i(&O, rangeVisited);
    buf_s(&O, ",\"ruleCount\":"); buf_i(&O, ruleCount);
    buf_s(&O, ",\"regionChecks\":"); buf_i(&O, regChecks);
    buf_s(&O, "}}");
}

int hh_day(int y, int d) {
    if (y < 0 || y >= g_nyears) y = 0;
    if (d < 0) d = 0;
    if (d >= g_ndays) d = g_ndays - 1;
    loadDay(y, d);
    analyze();
    refreshWatchValues();
    ob(); writeDay();
    return O.len;
}

int hh_state(void) { ob(); writeDay(); return O.len; }

int hh_threshold(int t10) {
    thr = (float)t10 / 10.0f;
    {
        Buf *t = trace_begin(5);
        buf_s(t, "threshold set to "); buf_f1(t, thr); buf_s(t, " C");
        trace_end();
    }
    analyze();
    ob(); writeDay();
    return O.len;
}

/* the selected cell: its history becomes a linked list (Exp 2) */
int hh_select(int cell) {
    int d, v, n = 0;
    Cell *p;
    ob();
    if (cell < 0 || cell >= g_ncells) { buf_s(&O, "{\"ok\":0}"); return O.len; }
    selCell = cell;
    p = cellByIndex(cell);
    freeList(&history);
    for (d = day; d >= 0 && n < HIST_DAYS; d--, n++) {
        char lab[24]; Buf b; float t = cell_value(yi, d, cell, &v);
        buf_init(&b, lab, sizeof lab); dateLabel(&b, d);
        insertBegin(&history, lab, v ? t : -99.0f);        /* oldest ends up at head */
    }
    {
        Buf *t = trace_begin(2);
        buf_s(t, "insertBegin(&history, ...) x"); buf_i(t, n); buf_s(t, "  cell [");
        buf_i(t, p->row); buf_s(t, "]["); buf_i(t, p->col); buf_s(t, "]  &grid[r][c]=");
        buf_hex(t, (unsigned long)p);
        trace_end();
    }
    buf_s(&O, "{\"ok\":1,\"cell\":"); buf_i(&O, cell);
    buf_s(&O, ",\"row\":"); buf_i(&O, p->row);
    buf_s(&O, ",\"col\":"); buf_i(&O, p->col);
    buf_s(&O, ",\"lat\":"); buf_f1(&O, p->lat);
    buf_s(&O, ",\"lon\":"); buf_f1(&O, p->lon);
    buf_s(&O, ",\"valid\":"); buf_i(&O, p->valid);
    buf_s(&O, ",\"t\":"); buf_f1(&O, p->valid ? p->tmax : 0);
    buf_s(&O, ",\"near\":"); { char nm[48]; Buf b; buf_init(&b, nm, sizeof nm); cityName(&b, cell); j_str(&O, nm); }
    buf_s(&O, ",\"streak\":"); buf_i(&O, p->valid ? streak(cell, day) : 0);
    buf_s(&O, ",\"addr\":\""); buf_hex(&O, (unsigned long)p); buf_c(&O, '"');
    buf_s(&O, ",\"history\":");
    { int saved = g_verbose; g_verbose = 0; display(history, &O); g_verbose = saved; }
    buf_c(&O, '}');
    return O.len;
}

/* ---------- alert rule (Exp 3) ---------- */
int hh_rule(void) {
    Token tk[MAX_TOK], pf[MAX_TOK];
    char err[64];
    char stepsMem[12000]; Buf steps;
    int n, pn = -1;
    ob();
    buf_init(&steps, stepsMem, sizeof stepsMem);
    n = tokenize(IN, tk, MAX_TOK, err);
    if (n > 0) {
        pn = toPostfix(tk, n, pf, &steps);
        if (pn > 0) {      /* dry-run evaluation to catch "T >=" style mistakes */
            int ok; evalPostfix(pf, pn, 45, 2, 25, 77, &ok);
            if (!ok) { pn = -1; strcopy(err, "incomplete expression", 64); }
        } else if (pn < 0) strcopy(err, "brackets don't match", 64);
    } else if (n == 0) strcopy(err, "empty rule", 64);
    if (pn <= 0) {
        buf_s(&O, "{\"ok\":0,\"error\":"); j_str(&O, err); buf_c(&O, '}');
        return O.len;
    }
    memcpy(rulePf, pf, sizeof(Token) * (hsize)pn); ruleN = pn;
    strcopy(ruleSrc, IN, sizeof ruleSrc);
    analyze();
    buf_s(&O, "{\"ok\":1,\"postfix\":\""); postfixText(rulePf, ruleN, &O);
    buf_s(&O, "\",\"steps\":"); buf_s(&O, stepsMem);
    buf_s(&O, ",\"state\":"); writeDay();
    buf_c(&O, '}');
    return O.len;
}

/* ---------- years ranked (Exp 7: merge sort) ---------- */
static float yPeak[MAX_YEARS]; static int yPeakDay[MAX_YEARS], yCount[MAX_YEARS];
static void yearCounts(long *checks) {
    int y, d, i, v; long c = 0;
    for (y = 0; y < g_nyears; y++) {
        yCount[y] = 0; yPeak[y] = -99; yPeakDay[y] = 0;
        for (d = 0; d < g_ndays; d++)
            for (i = 0; i < g_ncells; i++) {
                float t = cell_value(y, d, i, &v);
                if (!v) continue;
                c++;
                if (t >= thr) yCount[y]++;
                if (t > yPeak[y]) { yPeak[y] = t; yPeakDay[y] = d; }
            }
    }
    if (checks) *checks = c;
}

int hh_years(void) {
    Item it[MAX_YEARS]; int k; long checks;
    yearCounts(&checks);
    for (k = 0; k < g_nyears; k++) { it[k].key = (float)yCount[k]; it[k].id = k; }
    g_cmp = 0;
    mergeSort(it, g_nyears);
    reverseItems(it, g_nyears);
    {
        Buf *t = trace_begin(7);
        buf_s(t, "mergeSort(years, "); buf_i(t, g_nyears); buf_s(t, ") by heatwave cell-days  "); buf_i(t, g_cmp);
        buf_s(t, " comparisons  (counted "); buf_i(t, checks); buf_s(t, " cell-days)");
        trace_end();
    }
    ob();
    buf_s(&O, "{\"thr\":"); buf_f1(&O, thr); buf_s(&O, ",\"years\":[");
    for (k = 0; k < g_nyears; k++) {
        int y = it[k].id;
        if (k) buf_c(&O, ',');
        buf_s(&O, "{\"year\":"); buf_i(&O, g_years[y]); buf_s(&O, ",\"yi\":"); buf_i(&O, y);
        buf_s(&O, ",\"count\":"); buf_i(&O, yCount[y]);
        buf_s(&O, ",\"peak\":"); buf_f1(&O, yPeak[y]);
        buf_s(&O, ",\"peakDay\":"); buf_i(&O, yPeakDay[y]);
        buf_s(&O, ",\"peakDate\":\""); dateLabel(&O, yPeakDay[y]); buf_s(&O, "\"}");
    }
    buf_s(&O, "]}");
    return O.len;
}

/* ---------- watchlist edits (Exp 2 on the live list) ---------- */
static int splitArgs(char *s, char **parts, int max) {
    int n = 0;
    parts[n++] = s;
    while (*s && n < max) { if (*s == '|') { *s = 0; parts[n++] = s + 1; } s++; }
    return n;
}

static int knownCity(const char *name, char *canon) {
    int slot, probes, ci = hashSearch(&cityTable, name, &slot, &probes);
    if (ci < 0) return -1;
    strcopy(canon, CITIES[ci].name, 24);
    return ci;
}

int hh_watch(void) {
    char *a[4]; int n = splitArgs(IN, a, 4), ok = 0;
    char c1[24], c2[24];
    const char *msg = "";
    g_verbose = 1;
    if (streq(a[0], "insertBegin") && n >= 2) {
        if (knownCity(a[1], c1) < 0) msg = "unknown city";
        else { insertBegin(&watch, c1, 0); ok = 1; }
    } else if (streq(a[0], "insertAfter") && n >= 3) {
        if (knownCity(a[2], c2) < 0) msg = "unknown city";
        else { ok = insertAfter(watch, a[1], c2, 0); if (!ok) msg = "key not in list"; }
    } else if (streq(a[0], "deleteBefore") && n >= 2) {
        ok = deleteBefore(&watch, a[1]);
        if (!ok) msg = "nothing before that city (or not in list)";
    }
    g_verbose = 0;
    refreshWatchValues();
    ob();
    buf_s(&O, "{\"ok\":"); buf_i(&O, ok); buf_s(&O, ",\"msg\":"); j_str(&O, msg);
    buf_s(&O, ",\"watch\":"); writeWatch(); buf_c(&O, '}');
    return O.len;
}

/* ======================= HeatQL search ======================= */
static int parseDate(const char *s, int *py, int *pd) {
    int dd = 0, mm = 0, yy = 0, k, i;
    for (i = 0; is_digit(s[i]); i++) dd = dd * 10 + (s[i] - '0');
    if (s[i] != '-' && s[i] != '/') return 0;
    for (i++; is_digit(s[i]); i++) mm = mm * 10 + (s[i] - '0');
    if (s[i] != '-' && s[i] != '/') return 0;
    for (i++; is_digit(s[i]); i++) yy = yy * 10 + (s[i] - '0');
    if (s[i]) return 0;
    if (mm < 3 || mm > 6 || dd < 1 || dd > MLEN[mm - 3]) return -1;
    for (k = 0; k < g_nyears; k++) if (g_years[k] == yy) break;
    if (k == g_nyears) return -2;
    *py = k; *pd = dd - 1;
    for (i = 0; i < mm - 3; i++) *pd += MLEN[i];
    return 1;
}

static void rowStart(int *first) { if (!*first) buf_c(&O, ','); *first = 0; buf_c(&O, '['); }
static void cellRow(int rank, int cell, int *first) {
    Cell *p = cellByIndex(cell); char tmp[64]; Buf b;
    rowStart(first);
    buf_c(&O, '"'); buf_i(&O, rank); buf_s(&O, "\",");
    buf_init(&b, tmp, sizeof tmp); buf_f1(&b, p->lat); buf_s(&b, "N "); buf_f1(&b, p->lon); buf_c(&b, 'E'); j_str(&O, tmp);
    buf_c(&O, ',');
    buf_init(&b, tmp, sizeof tmp); if (cellCity[cell] >= 0) buf_s(&b, CITIES[cellCity[cell]].name); else buf_s(&b, "-"); j_str(&O, tmp);
    buf_c(&O, ',');
    buf_c(&O, '"'); buf_f1(&O, p->tmax); buf_s(&O, "\",");
    j_str(&O, p->tmax >= thr + 2 ? "Severe" : p->tmax >= thr ? "Heatwave" : "-");
    buf_c(&O, ']');
}

static void plan(int *first, const char *title, const char *exp, const char *detail) {
    if (!*first) buf_c(&O, ','); *first = 0;
    buf_s(&O, "{\"title\":"); j_str(&O, title); buf_s(&O, ",\"exp\":"); j_str(&O, exp);
    buf_s(&O, ",\"detail\":"); j_str(&O, detail); buf_c(&O, '}');
}
static void race(int *first, const char *name, const char *exp, long count) {
    if (!*first) buf_c(&O, ','); *first = 0;
    buf_s(&O, "{\"name\":"); j_str(&O, name); buf_s(&O, ",\"exp\":"); j_str(&O, exp);
    buf_s(&O, ",\"count\":"); buf_i(&O, count); buf_c(&O, '}');
}

static int strcmpi(const char *a, const char *b) {
    while (*a && to_lower(*a) == to_lower(*b)) { a++; b++; }
    g_cmp++;
    return (int)(unsigned char)to_lower(*a) - (int)(unsigned char)to_lower(*b);
}

int hh_query(void) {
    char q[256]; char *w[16]; int nw = 0, i, qy = yi, qd = day, hasDate = 0;
    static Item items[MAXC], work[MAXC];
    static int cells[MAXC];
    char tmp[200]; Buf b;
    int first;

    /* lower-case copy, split on spaces */
    for (i = 0; IN[i] && i < 255; i++) q[i] = to_lower(IN[i]);
    q[i] = 0;
    {
        char *s = q;
        while (*s && nw < 16) {
            while (is_space(*s)) *s++ = 0;
            if (!*s) break;
            w[nw++] = s;
            while (*s && !is_space(*s)) s++;
        }
    }
    ob();
    if (nw == 0) { buf_s(&O, "{\"ok\":0,\"error\":\"type a search\"}"); return O.len; }

    /* optional "on DD-MM-YYYY" at the end */
    if (nw >= 2 && streq(w[nw - 2], "on")) {
        int r = parseDate(w[nw - 1], &qy, &qd);
        if (r != 1) {
            buf_s(&O, "{\"ok\":0,\"error\":");
            j_str(&O, r == -1 ? "date must be between 1 March and 30 June" :
                      r == -2 ? "no data for that year (2015-2025 available)" : "date format is DD-MM-YYYY");
            buf_c(&O, '}');
            return O.len;
        }
        hasDate = 1; nw -= 2;
    }
    if (hasDate && (qy != yi || qd != day)) { loadDay(qy, qd); analyze(); refreshWatchValues(); }

    /* ---- above X ---- */
    if (streq(w[0], "above") && nw == 2) {
        int ok, visited = 0, n, steps, k, idx; long linear = 0, sortCmp, bsCmp;
        float x = parse_float(w[1], &ok);
        if (!ok) { buf_s(&O, "{\"ok\":0,\"error\":\"above needs a number, e.g. above 45\"}"); return O.len; }
        g_cmp = 0;
        n = rangeSearch(dayTree, x, cells, 0, MAXC, &visited);
        for (i = 0; i < g_ncells; i++) if (cellByIndex(i)->valid) { linear++; }
        k = 0;
        for (i = 0; i < g_ncells; i++) { Cell *p = cellByIndex(i); if (p->valid) { items[k].key = p->tmax; items[k].id = i; k++; } }
        g_cmp = 0; mergeSort(items, k); sortCmp = g_cmp;
        g_cmp = 0; idx = binarySearch(items, k, x, &steps); bsCmp = g_cmp;
        (void)idx;
        { Buf *t = trace_begin(5); buf_s(t, "rangeSearch(root, "); buf_f1(t, x); buf_s(t, ") -> "); buf_i(t, n);
          buf_s(t, " cells, visited "); buf_i(t, visited); buf_s(t, " nodes"); trace_end(); }
        buf_s(&O, "{\"ok\":1,\"kind\":\"above\",\"title\":\"");
        buf_s(&O, "Cells at or above "); buf_f1(&O, x); buf_s(&O, " C\",\"meta\":\"");
        buf_i(&O, n); buf_s(&O, " of "); buf_i(&O, k); buf_s(&O, " cells with data  "); dateLabel(&O, day); buf_c(&O, ' '); buf_i(&O, g_years[yi]);
        buf_s(&O, "\",\"cols\":[\"#\",\"Cell\",\"Near\",\"Max C\",\"Level\"],\"rows\":[");
        first = 1;
        for (i = n - 1; i >= 0 && n - i <= 15; i--) cellRow(n - i, cells[i], &first);
        buf_s(&O, "],\"more\":"); buf_i(&O, n > 15 ? n - 15 : 0);
        buf_s(&O, ",\"cells\":["); for (i = 0; i < n; i++) { if (i) buf_c(&O, ','); buf_i(&O, cells[i]); }
        buf_s(&O, "],\"plan\":["); first = 1;
        plan(&first, "Parse", "tokenizer", "[above] [number] [on date] -> RANGE(T >= x)");
        plan(&first, "Pick a structure", "EXP 5", "range query -> binary search tree keyed on T");
        buf_init(&b, tmp, sizeof tmp); buf_s(&b, "rangeSearch(root, "); buf_f1(&b, x); buf_s(&b, ") visited "); buf_i(&b, visited); buf_s(&b, " nodes");
        plan(&first, "Run", "EXP 5", tmp);
        buf_s(&O, "],\"race\":["); first = 1;
        race(&first, "Linear scan of the array", "EXP 1", linear);
        buf_init(&b, tmp, sizeof tmp); buf_s(&b, "Binary search (after sorting: "); buf_i(&b, sortCmp); buf_s(&b, " cmp)");
        race(&first, tmp, "EXP 7", bsCmp + n);
        race(&first, "BST range search", "EXP 5", visited);
        buf_s(&O, "]");
    }
    /* ---- hottest years ---- */
    else if (streq(w[0], "hottest") && nw >= 2 && streq(w[1], "years")) {
        Item it[MAX_YEARS]; long checks, bstCost = 0; int y, d, k;
        yearCounts(&checks);
        for (k = 0; k < g_nyears; k++) { it[k].key = (float)yCount[k]; it[k].id = k; }
        g_cmp = 0; mergeSort(it, g_nyears); reverseItems(it, g_nyears);
        { long msCmp = g_cmp;
          /* the same counting done with a BST per day, for the race */
          for (y = 0; y < g_nyears; y++)
              for (d = 0; d < g_ndays; d++) {
                  BNode *root = 0; int v, vis = 0;
                  g_cmp = 0;
                  for (i = 0; i < g_ncells; i++) { int ci = insertOrder(i); float t = cell_value(y, d, ci, &v); if (v) root = bstInsert(root, t, ci); }
                  rangeSearch(root, thr, cells, 0, MAXC, &vis);
                  bstCost += g_cmp;
                  bstFree(root);
              }
          { Buf *t = trace_begin(7); buf_s(t, "mergeSort(years, "); buf_i(t, g_nyears); buf_s(t, ")  "); buf_i(t, msCmp); buf_s(t, " comparisons"); trace_end(); }
          buf_s(&O, "{\"ok\":1,\"kind\":\"years\",\"title\":\"Years ranked by heatwave cell-days\",\"meta\":\"1 Mar - 30 Jun, cells at or above ");
          buf_f1(&O, thr); buf_s(&O, " C\",\"cols\":[\"#\",\"Year\",\"Cell-days\",\"Peak day\",\"Peak C\"],\"rows\":[");
          first = 1;
          for (k = 0; k < g_nyears; k++) {
              y = it[k].id; rowStart(&first);
              buf_c(&O, '"'); buf_i(&O, k + 1); buf_s(&O, "\",\""); buf_i(&O, g_years[y]); buf_s(&O, "\",\"");
              buf_i(&O, yCount[y]); buf_s(&O, "\",\""); dateLabel(&O, yPeakDay[y]); buf_s(&O, "\",\""); buf_f1(&O, yPeak[y]); buf_s(&O, "\"]");
          }
          buf_s(&O, "],\"more\":0,\"cells\":[],\"plan\":["); first = 1;
          plan(&first, "Parse", "tokenizer", "[hottest] [years] -> RANK_YEARS");
          plan(&first, "Count", "EXP 1", "scan every cell-day of every year once");
          buf_init(&b, tmp, sizeof tmp); buf_s(&b, "mergeSort(years, "); buf_i(&b, g_nyears); buf_s(&b, ") descending");
          plan(&first, "Rank", "EXP 7", tmp);
          buf_s(&O, "],\"race\":["); first = 1;
          race(&first, "Count by scanning each cell-day once", "EXP 1", checks);
          race(&first, "Build a BST each day, then range search", "EXP 5", bstCost);
          race(&first, "Rank the years (merge sort)", "EXP 7", msCmp);
          buf_s(&O, "],\"note\":\"Here the plain scan wins: a BST only pays off when the same tree answers many queries.\"");
        }
    }
    /* ---- hottest N ---- */
    else if (streq(w[0], "hottest") && nw == 2) {
        int ok, N = parse_int(w[1], &ok), k = 0; long ins, qs, ms;
        if (!ok || N < 1) { buf_s(&O, "{\"ok\":0,\"error\":\"hottest needs a count, e.g. hottest 10\"}"); return O.len; }
        for (i = 0; i < g_ncells; i++) { Cell *p = cellByIndex(i); if (p->valid) { items[k].key = p->tmax; items[k].id = i; k++; } }
        memcpy(work, items, sizeof(Item) * (hsize)k); g_cmp = 0; insertionSort(work, k); ins = g_cmp;
        memcpy(work, items, sizeof(Item) * (hsize)k); g_cmp = 0; mergeSort(work, k); ms = g_cmp;
        memcpy(work, items, sizeof(Item) * (hsize)k); g_cmp = 0; quickSort(work, 0, k - 1); qs = g_cmp;
        reverseItems(work, k);
        if (N > k) N = k;
        { Buf *t = trace_begin(7); buf_s(t, "quickSort(grid, "); buf_i(t, k); buf_s(t, ")  "); buf_i(t, qs); buf_s(t, " comparisons, take top "); buf_i(t, N); trace_end(); }
        buf_s(&O, "{\"ok\":1,\"kind\":\"hottest\",\"title\":\"The "); buf_i(&O, N); buf_s(&O, " hottest cells\",\"meta\":\"sorted ");
        buf_i(&O, k); buf_s(&O, " cells  "); dateLabel(&O, day); buf_c(&O, ' '); buf_i(&O, g_years[yi]);
        buf_s(&O, "\",\"cols\":[\"#\",\"Cell\",\"Near\",\"Max C\",\"Level\"],\"rows\":[");
        first = 1;
        for (i = 0; i < N && i < 25; i++) cellRow(i + 1, work[i].id, &first);
        buf_s(&O, "],\"more\":"); buf_i(&O, N > 25 ? N - 25 : 0);
        buf_s(&O, ",\"cells\":["); for (i = 0; i < N; i++) { if (i) buf_c(&O, ','); buf_i(&O, work[i].id); }
        buf_s(&O, "],\"plan\":["); first = 1;
        plan(&first, "Parse", "tokenizer", "[hottest] [count] [on date] -> TOP(n)");
        plan(&first, "Pick a structure", "EXP 7", "top-n -> sort the day's cells, take the first n");
        buf_init(&b, tmp, sizeof tmp); buf_s(&b, "quickSort(grid, "); buf_i(&b, k); buf_s(&b, "), reverse, first "); buf_i(&b, N);
        plan(&first, "Run", "EXP 7", tmp);
        buf_s(&O, "],\"race\":["); first = 1;
        race(&first, "Insertion sort (baseline)", "EXP 7", ins);
        race(&first, "Quick sort", "EXP 7", qs);
        race(&first, "Merge sort", "EXP 7", ms);
        buf_s(&O, "]");
    }
    /* ---- find city ---- */
    else if (streq(w[0], "find") && nw >= 2) {
        char name[48]; int slot, probes, ci, lin = 0, steps = 0, lo = 0, hi = NCITIES, v;
        static int sortedIdx[96]; static int sortedReady = 0;
        buf_init(&b, name, sizeof name);
        for (i = 1; i < nw; i++) { if (i > 1) buf_c(&b, ' '); buf_s(&b, w[i]); }
        g_cmp = 0; ci = hashSearch(&cityTable, name, &slot, &probes);
        for (i = 0; i < NCITIES; i++) { lin++; if (streqi(CITIES[i].name, name)) break; }
        if (!sortedReady) {          /* names in alphabetical order, once */
            int a, c;
            for (a = 0; a < NCITIES; a++) sortedIdx[a] = a;
            for (a = 1; a < NCITIES; a++) { int x = sortedIdx[a]; c = a - 1;
                while (c >= 0 && strcmpi(CITIES[sortedIdx[c]].name, CITIES[x].name) > 0) { sortedIdx[c + 1] = sortedIdx[c]; c--; }
                sortedIdx[c + 1] = x; }
            sortedReady = 1;
        }
        while (lo < hi) { int mid = (lo + hi) / 2, r; steps++; r = strcmpi(CITIES[sortedIdx[mid]].name, name);
            if (r == 0) break; if (r < 0) lo = mid + 1; else hi = mid; }
        { Buf *t = trace_begin(8); buf_s(t, "hashSearch(cityTable, \""); buf_s(t, name); buf_s(t, "\") -> ");
          if (ci >= 0) { buf_s(t, "slot "); buf_i(t, slot); } else buf_s(t, "not found"); buf_s(t, ", probes "); buf_i(t, probes); trace_end(); }
        if (ci < 0) {
            buf_s(&O, "{\"ok\":0,\"error\":\"no city called '"); buf_s(&O, name); buf_s(&O, "' in the table\"}");
            return O.len;
        }
        {
            int cell = cityCell[ci]; Cell *p = cellByIndex(cell); float t = todayT(cell, &v);
            buf_s(&O, "{\"ok\":1,\"kind\":\"find\",\"title\":"); j_str(&O, CITIES[ci].name);
            buf_s(&O, ",\"meta\":\"home slot ["); buf_i(&O, (int)hashKey(name, cityTable.cap)); buf_s(&O, "] -> stored at [");
            buf_i(&O, slot); buf_s(&O, "]\",\"cols\":[\"#\",\"Cell\",\"Near\",\"Max C\",\"Level\"],\"rows\":[");
            first = 1; rowStart(&first);
            buf_s(&O, "\"1\",\""); buf_f1(&O, p->lat); buf_s(&O, "N "); buf_f1(&O, p->lon); buf_s(&O, "E\",");
            j_str(&O, CITIES[ci].name); buf_s(&O, ",\"");
            if (v) buf_f1(&O, t); else buf_s(&O, "no data"); buf_s(&O, "\",");
            j_str(&O, !v ? "-" : t >= thr + 2 ? "Severe" : t >= thr ? "Heatwave" : "-"); buf_c(&O, ']');
            buf_s(&O, "],\"more\":0,\"cells\":["); buf_i(&O, cell); buf_s(&O, "],\"select\":"); buf_i(&O, cell);
            buf_s(&O, ",\"plan\":["); first = 1;
            plan(&first, "Parse", "tokenizer", "[find] [name] -> LOOKUP(name)");
            plan(&first, "Pick a structure", "EXP 8", "exact key -> hash table (circular array, linear probing)");
            buf_init(&b, tmp, sizeof tmp); buf_s(&b, "hashSearch(cityTable, \""); buf_s(&b, name); buf_s(&b, "\") slot ");
            buf_i(&b, slot); buf_s(&b, ", "); buf_i(&b, probes); buf_s(&b, " probe(s)");
            plan(&first, "Run", "EXP 8", tmp);
            buf_s(&O, "],\"race\":["); first = 1;
            race(&first, "Linear search, city array", "EXP 1", lin);
            race(&first, "Binary search, sorted names", "EXP 7", steps);
            race(&first, "Hash table", "EXP 8", probes);
            buf_s(&O, "]");
        }
    }
    /* ---- spread from city ---- */
    else if (streq(w[0], "spread") && nw >= 3 && streq(w[1], "from")) {
        char name[48]; int slot, probes, ci, cell, n, checks, maxL = 0, perLevel[64];
        buf_init(&b, name, sizeof name);
        for (i = 2; i < nw; i++) { if (i > 2) buf_c(&b, ' '); buf_s(&b, w[i]); }
        ci = hashSearch(&cityTable, name, &slot, &probes);
        if (ci < 0) { buf_s(&O, "{\"ok\":0,\"error\":\"unknown city\"}"); return O.len; }
        cell = cityCell[ci];
        if (!hot[cell]) {
            buf_s(&O, "{\"ok\":0,\"error\":\""); buf_s(&O, CITIES[ci].name);
            buf_s(&O, " is below the heatwave threshold on that day, so there is nothing to spread from\"}");
            return O.len;
        }
        n = bfs(&G, cell, hot, order, level, &checks);
        for (i = 0; i < 64; i++) perLevel[i] = 0;
        for (i = 0; i < n; i++) { int L = level[order[i]]; if (L < 64) perLevel[L]++; if (L > maxL) maxL = L; }
        { Buf *t = trace_begin(6); buf_s(t, "bfs(adj, "); buf_s(t, CITIES[ci].name); buf_s(t, ") -> "); buf_i(t, n);
          buf_s(t, " hot cells in "); buf_i(t, maxL + 1); buf_s(t, " levels, "); buf_i(t, checks); buf_s(t, " matrix checks"); trace_end(); }
        buf_s(&O, "{\"ok\":1,\"kind\":\"spread\",\"title\":\"Heat connected to "); buf_s(&O, CITIES[ci].name);
        buf_s(&O, "\",\"meta\":\""); buf_i(&O, n); buf_s(&O, " cells, "); buf_i(&O, maxL + 1); buf_s(&O, " BFS levels\"");
        buf_s(&O, ",\"cols\":[\"Level\",\"Cells\",\"Example\",\"\",\"\"],\"rows\":[");
        first = 1;
        for (i = 0; i <= maxL && i < 64; i++) {
            int k, ex = -1; for (k = 0; k < n; k++) if (level[order[k]] == i) { ex = order[k]; break; }
            rowStart(&first); buf_c(&O, '"'); buf_i(&O, i); buf_s(&O, "\",\""); buf_i(&O, perLevel[i]); buf_s(&O, "\",");
            { char nm[48]; Buf bb; buf_init(&bb, nm, sizeof nm); cityName(&bb, ex); j_str(&O, nm); }
            buf_s(&O, ",\"\",\"\"]");
        }
        buf_s(&O, "],\"more\":0,\"cells\":["); for (i = 0; i < n; i++) { if (i) buf_c(&O, ','); buf_i(&O, order[i]); }
        buf_s(&O, "],\"levels\":["); for (i = 0; i < n; i++) { if (i) buf_c(&O, ','); buf_i(&O, level[order[i]]); }
        buf_s(&O, "],\"plan\":["); first = 1;
        plan(&first, "Parse", "tokenizer", "[spread] [from] [city] -> BFS(city)");
        plan(&first, "Look up the city", "EXP 8", "hashSearch(cityTable, name) -> grid cell");
        buf_init(&b, tmp, sizeof tmp); buf_s(&b, "bfs(adj, start) over cells >= "); buf_f1(&b, thr); buf_s(&b, " C, queue from Exp 4");
        plan(&first, "Run", "EXP 6", tmp);
        buf_s(&O, "],\"race\":[]");
    }
    /* ---- where EXPR ---- */
    else if (streq(w[0], "where") && nw >= 2) {
        Token tk[MAX_TOK], pf[MAX_TOK]; char err[64], expr[200]; int n, pn, k = 0, ok;
        char stepsMem[8000]; Buf steps;
        buf_init(&b, expr, sizeof expr);
        for (i = 1; i < nw; i++) { if (i > 1) buf_c(&b, ' '); buf_s(&b, w[i]); }
        buf_init(&steps, stepsMem, sizeof stepsMem);
        n = tokenize(expr, tk, MAX_TOK, err);
        pn = n > 0 ? toPostfix(tk, n, pf, &steps) : -1;
        if (pn > 0) { evalPostfix(pf, pn, 45, 2, 25, 77, &ok); if (!ok) pn = -1; }
        if (pn <= 0) { buf_s(&O, "{\"ok\":0,\"error\":\"could not read that condition: "); buf_s(&O, n < 0 ? err : "check the operators"); buf_s(&O, "\"}"); return O.len; }
        for (i = 0; i < g_ncells; i++) {
            Cell *p = cellByIndex(i);
            if (p->valid && evalPostfix(pf, pn, p->tmax, (float)streak(i, day), p->lat, p->lon, &ok) != 0 && ok) { items[k].key = p->tmax; items[k].id = i; k++; }
        }
        g_cmp = 0; if (k > 1) quickSort(items, 0, k - 1); reverseItems(items, k);
        buf_s(&O, "{\"ok\":1,\"kind\":\"where\",\"title\":\"Cells where ");
        for (i = 0; expr[i]; i++) { if (expr[i] == '"' || expr[i] == '\\') buf_c(&O, '\\'); buf_c(&O, expr[i]); }
        buf_s(&O, "\",\"meta\":\""); buf_i(&O, k); buf_s(&O, " cells match  postfix: "); postfixText(pf, pn, &O);
        buf_s(&O, "\",\"cols\":[\"#\",\"Cell\",\"Near\",\"Max C\",\"Level\"],\"rows\":[");
        first = 1;
        for (i = 0; i < k && i < 15; i++) cellRow(i + 1, items[i].id, &first);
        buf_s(&O, "],\"more\":"); buf_i(&O, k > 15 ? k - 15 : 0);
        buf_s(&O, ",\"cells\":["); for (i = 0; i < k; i++) { if (i) buf_c(&O, ','); buf_i(&O, items[i].id); }
        buf_s(&O, "],\"steps\":"); buf_s(&O, stepsMem);
        buf_s(&O, ",\"plan\":["); first = 1;
        plan(&first, "Parse the condition", "EXP 3", "tokenize, then infix -> postfix with a stack");
        plan(&first, "Evaluate", "EXP 3", "evalPostfix on every cell (a value stack per cell)");
        plan(&first, "Order the matches", "EXP 7", "quickSort by temperature, hottest first");
        buf_s(&O, "],\"race\":[]");
    }
    else {
        buf_s(&O, "{\"ok\":0,\"error\":\"try: above 45 on 28-05-2024 | hottest 10 | hottest years | find delhi | spread from churu | where T >= 46 && lat > 25\"}");
        return O.len;
    }
    buf_s(&O, ",\"jump\":{\"yi\":"); buf_i(&O, yi); buf_s(&O, ",\"day\":"); buf_i(&O, day); buf_s(&O, "}}");
    return O.len;
}

/* ======================= trace ======================= */
int hh_trace(void) { ob(); trace_drain_json(&O); return O.len; }

/* ======================= Lab mode ======================= */
static Node *labList = 0;
static Stack labStack; static int labStackReady = 0;
static CQueue labQ; static int labQReady = 0;
static BNode *labTree = 0;
static Graph LG; static unsigned char LGm[64]; static int lgReady = 0;
static int lgOrder[8], lgLevel[8], lgN = 0;
static const char *LGNAMES[8] = {"Delhi", "Jaipur", "Agra", "Gwalior", "Kota", "Jhansi", "Bhopal", "Lucknow"};
static Item labArr[16], labOrig[16]; static int labN = 0, labSorted = 0; static long labCmp = 0; static int labFound = -2, labSteps = 0;
static HashTable labHash; static int labHashReady = 0;
static int lastHome = -1, lastSlot = -1, lastProbes = 0, lastFound = -1;
static char lastKey[24] = "";
static Cell labCopy; static Cell *labOrigPtr = 0;

static void labTreeJson(BNode *r) {
    static BNode *nodes[256]; int n, i;
    n = inorder(r, nodes, 0);
    buf_s(&O, "\"nodes\":[");
    for (i = 0; i < n; i++) {
        BNode *p = nodes[i]; int depth = 0; BNode *c = r;
        while (c && c != p) { depth++; c = (p->key < c->key) ? c->left : c->right; if (!c) break; }
        if (i) buf_c(&O, ',');
        buf_s(&O, "{\"id\":"); buf_i(&O, bstNodeId(p)); buf_s(&O, ",\"key\":"); buf_f1(&O, p->key);
        buf_s(&O, ",\"x\":"); buf_i(&O, i); buf_s(&O, ",\"depth\":"); buf_i(&O, depth);
        buf_s(&O, ",\"l\":"); buf_i(&O, bstNodeId(p->left)); buf_s(&O, ",\"r\":"); buf_i(&O, bstNodeId(p->right)); buf_c(&O, '}');
    }
    buf_s(&O, "],\"in\":\""); for (i = 0; i < n; i++) { if (i) buf_c(&O, ' '); buf_f1(&O, nodes[i]->key); }
    n = preorder(r, nodes, 0);
    buf_s(&O, "\",\"pre\":\""); for (i = 0; i < n; i++) { if (i) buf_c(&O, ' '); buf_f1(&O, nodes[i]->key); }
    n = postorder(r, nodes, 0);
    buf_s(&O, "\",\"post\":\""); for (i = 0; i < n; i++) { if (i) buf_c(&O, ' '); buf_f1(&O, nodes[i]->key); }
    n = levelOrder(r, nodes, 256);
    buf_s(&O, "\",\"level\":\""); for (i = 0; i < n; i++) { if (i) buf_c(&O, ' '); buf_f1(&O, nodes[i]->key); }
    buf_s(&O, "\",\"height\":"); buf_i(&O, bstHeight(r));
}

int hh_lab(void) {
    char *a[5]; int n = splitArgs(IN, a, 5), exp, ok = 1, okn;
    const char *msg = "";
    char msgbuf[160]; Buf mb;
    buf_init(&mb, msgbuf, sizeof msgbuf);
    exp = parse_int(a[0], &okn);
    g_verbose = 1;
    ob();
    buf_s(&O, "{\"exp\":"); buf_i(&O, exp);

    if (exp == 1) {
        int okLat = 1, okLon = 1;
        float lat = n > 2 ? parse_float(a[2], &okLat) : 28.5f, lon = n > 3 ? parse_float(a[3], &okLon) : 77.5f;
        Cell *p = (okLat && okLon) ? getCell(lat, lon) : 0;
        if (!okLat || !okLon) { ok = 0; msg = "lat and lon must be numbers"; }
        else if (!p) { ok = 0; msg = "outside the 31 x 31 grid"; }
        else {
            Buf *t = trace_begin(1);
            buf_s(t, "getCell("); buf_f1(t, lat); buf_s(t, ", "); buf_f1(t, lon); buf_s(t, ") -> &grid["); buf_i(t, p->row);
            buf_s(t, "]["); buf_i(t, p->col); buf_s(t, "] = base + ("); buf_i(t, p->row); buf_s(t, "*31 + "); buf_i(t, p->col);
            buf_s(t, ") * sizeof(Cell) = "); buf_hex(t, (unsigned long)p);
            trace_end();
            if (streq(a[1], "update") && n > 4) {
                labCopy = *p; labOrigPtr = p;
                labCopy.tmax = parse_float(a[4], &okn);
                t = trace_begin(1);
                buf_s(t, "Cell copy = *p; copy.tmax = "); buf_f1(t, labCopy.tmax);
                buf_s(t, "   (the copy changes, grid[][] does not)");
                trace_end();
            }
            buf_s(&O, ",\"cell\":{\"row\":"); buf_i(&O, p->row); buf_s(&O, ",\"col\":"); buf_i(&O, p->col);
            buf_s(&O, ",\"lat\":"); buf_f1(&O, p->lat); buf_s(&O, ",\"lon\":"); buf_f1(&O, p->lon);
            buf_s(&O, ",\"tmax\":"); buf_f1(&O, p->tmax); buf_s(&O, ",\"valid\":"); buf_i(&O, p->valid);
            buf_s(&O, ",\"index\":"); buf_i(&O, p->index); buf_s(&O, ",\"addr\":\""); buf_hex(&O, (unsigned long)p);
            buf_s(&O, "\"},\"base\":\""); buf_hex(&O, (unsigned long)&grid[0][0]);
            buf_s(&O, "\",\"size\":"); buf_i(&O, (long)sizeof(Cell));
            buf_s(&O, ",\"copy\":");
            if (labOrigPtr == p) { buf_s(&O, "{\"tmax\":"); buf_f1(&O, labCopy.tmax); buf_s(&O, ",\"addr\":\""); buf_hex(&O, (unsigned long)&labCopy); buf_s(&O, "\"}"); }
            else buf_s(&O, "null");
        }
    }
    else if (exp == 2) {
        if (streq(a[1], "insertBegin") && n > 2) insertBegin(&labList, a[2], 0);
        else if (streq(a[1], "insertAfter") && n > 3) { ok = insertAfter(labList, a[2], a[3], 0); if (!ok) msg = "key not in list"; }
        else if (streq(a[1], "deleteBefore") && n > 2) { ok = deleteBefore(&labList, a[2]); if (!ok) msg = "nothing before that node"; }
        else if (streq(a[1], "reset")) { freeList(&labList); insertBegin(&labList, "Banda", 0); insertBegin(&labList, "Jhansi", 0); insertBegin(&labList, "Delhi", 0); insertBegin(&labList, "Churu", 0); }
        buf_s(&O, ",\"list\":"); display(labList, &O);
    }
    else if (exp == 3) {
        if (!labStackReady) { createStack(&labStack); labStackReady = 1; }
        if (streq(a[1], "convert") && n > 2) {
            Token tk[MAX_TOK], pf[MAX_TOK]; char err[64]; int tn = tokenize(a[2], tk, MAX_TOK, err), pn;
            char stepsMem[12000]; Buf steps; buf_init(&steps, stepsMem, sizeof stepsMem);
            if (tn <= 0) { ok = 0; msg = tn == 0 ? "empty" : err; }
            else {
                pn = toPostfix(tk, tn, pf, &steps);
                if (pn < 0) { ok = 0; msg = "brackets don't match"; }
                else {
                    float T = n > 3 ? parse_float(a[3], &okn) : 45, D = n > 4 ? parse_float(a[4], &okn) : 2;
                    int eok; float v = evalPostfix(pf, pn, T, D, 26.5f, 77.5f, &eok);
                    buf_s(&O, ",\"postfix\":\""); postfixText(pf, pn, &O);
                    buf_s(&O, "\",\"steps\":"); buf_s(&O, stepsMem);
                    buf_s(&O, ",\"value\":"); if (eok) buf_f1(&O, v); else buf_s(&O, "null");
                    { Buf *t = trace_begin(3); buf_s(t, "evalPostfix(T="); buf_f1(t, T); buf_s(t, ", days="); buf_f1(t, D); buf_s(t, ") -> ");
                      if (eok) buf_f1(t, v); else buf_s(t, "error"); trace_end(); }
                }
            }
        } else if (streq(a[1], "push") && n > 2) {
            Token t; memset(&t, 0, sizeof t); t.type = TOK_OP; strcopy(t.text, a[2], 8);
            push(&labStack, t);
            { Buf *tr = trace_begin(3); buf_s(tr, "push(&s, '"); buf_s(tr, t.text); buf_s(tr, "')  size "); buf_i(tr, labStack.size); trace_end(); }
        } else if (streq(a[1], "pop")) {
            if (isEmpty(&labStack)) { ok = 0; msg = "stack underflow: pop on an empty stack"; trace_line(3, "pop(&s)  underflow, stack is empty"); }
            else { Token t = pop(&labStack); Buf *tr = trace_begin(3); buf_s(tr, "pop(&s) -> '"); buf_s(tr, t.text); buf_s(tr, "'"); trace_end(); }
        } else if (streq(a[1], "peek")) {
            if (isEmpty(&labStack)) { ok = 0; msg = "stack is empty"; }
            else { Token t = peek(&labStack); Buf *tr = trace_begin(3); buf_s(tr, "peek(&s) -> '"); buf_s(tr, t.text); buf_s(tr, "'"); trace_end(); }
        } else if (streq(a[1], "reset")) { clearStack(&labStack); }
        buf_s(&O, ",\"stack\":[");
        { SNode *p; int k = 0; for (p = labStack.top; p; p = p->next, k++) {
            if (k) buf_c(&O, ','); buf_s(&O, "{\"v\":"); j_str(&O, p->tok.text); buf_s(&O, ",\"addr\":\""); buf_hex(&O, (unsigned long)p); buf_s(&O, "\"}"); } }
        buf_c(&O, ']');
    }
    else if (exp == 4) {
        if (!labQReady || streq(a[1], "reset")) { createQueue(&labQ, 6); labQReady = 1; trace_line(4, "createQueue(&q, 6)  front=0 rear=-1 count=0"); }
        if (streq(a[1], "enqueue") && n > 2 && (parse_int(a[2], &okn), !okn)) {
            ok = 0; msg = "value must be a whole number";
        } else if (streq(a[1], "enqueue") && n > 2) {
            int v = parse_int(a[2], &okn);
            Buf *t;
            ok = enqueue(&labQ, v);
            t = trace_begin(4);
            if (ok) { buf_s(t, "enqueue(&q, "); buf_i(t, v); buf_s(t, ")  rear=(rear+1)%6="); buf_i(t, labQ.rear); buf_s(t, " count="); buf_i(t, labQ.count); }
            else { buf_s(t, "enqueue(&q, "); buf_i(t, v); buf_s(t, ")  overflow: count == cap"); msg = "queue is full"; }
            trace_end();
        } else if (streq(a[1], "dequeue")) {
            int v; Buf *t;
            ok = dequeue(&labQ, &v);
            t = trace_begin(4);
            if (ok) { buf_s(t, "dequeue(&q) -> "); buf_i(t, v); buf_s(t, "  front now "); buf_i(t, labQ.front); buf_s(t, " count="); buf_i(t, labQ.count); }
            else { buf_s(t, "dequeue(&q)  underflow: count == 0"); msg = "queue is empty"; }
            trace_end();
        }
        buf_s(&O, ",\"cap\":"); buf_i(&O, labQ.cap); buf_s(&O, ",\"front\":"); buf_i(&O, labQ.front);
        buf_s(&O, ",\"rear\":"); buf_i(&O, labQ.rear); buf_s(&O, ",\"count\":"); buf_i(&O, labQ.count);
        buf_s(&O, ",\"slots\":[");
        { int i2, j; for (i2 = 0; i2 < labQ.cap; i2++) { int occ = 0; for (j = 0; j < labQ.count; j++) if ((labQ.front + j) % labQ.cap == i2) occ = 1;
            if (i2) buf_c(&O, ','); if (occ) buf_i(&O, labQ.items[i2]); else buf_s(&O, "null"); } }
        buf_c(&O, ']');
    }
    else if (exp == 5) {
        float k = n > 2 ? parse_float(a[2], &okn) : 0;
        int needKey = streq(a[1], "insert") || streq(a[1], "search") || streq(a[1], "delete");
        if (needKey && (n <= 2 || !okn)) { ok = 0; msg = "key must be a number, e.g. 44.5"; }
        else if (streq(a[1], "insert") && n > 2) { if (bstCount(labTree) >= 31) { ok = 0; msg = "lab tree holds up to 31 nodes"; } else labTree = bstInsert(labTree, k, -1); }
        else if (streq(a[1], "search") && n > 2) { BNode *f = bstSearch(labTree, k); if (!f) { ok = 0; msg = "not found"; } else { buf_s(&O, ",\"found\":"); buf_i(&O, bstNodeId(f)); } }
        else if (streq(a[1], "delete") && n > 2) {
            int del; labTree = bstDelete(labTree, k, &del);
            { Buf *t = trace_begin(5); buf_s(t, "bstDelete(root, "); buf_f1(t, k); buf_s(t, del ? ")  removed" : ")  not found"); trace_end(); }
            if (!del) { ok = 0; msg = "not found"; }
        }
        else if (streq(a[1], "reset")) {
            static const float seed[] = {45.2f, 41.0f, 47.6f, 38.4f, 43.1f, 46.0f, 48.3f};
            int i2;
            bstFree(labTree); labTree = 0;
            for (i2 = 0; i2 < 7; i2++) labTree = bstInsert(labTree, seed[i2], -1);
        }
        buf_c(&O, ','); labTreeJson(labTree);
    }
    else if (exp == 6) {
        if (!lgReady || streq(a[1], "reset")) {
            int saved = g_verbose; g_verbose = 0;
            graphInit(&LG, LGm, 8);
            addEdge(&LG, 0, 1); addEdge(&LG, 0, 2); addEdge(&LG, 1, 4); addEdge(&LG, 2, 3);
            addEdge(&LG, 3, 5); addEdge(&LG, 4, 6); addEdge(&LG, 5, 6); addEdge(&LG, 2, 7);
            g_verbose = saved; lgReady = 1; lgN = 0;
            trace_line(6, "graphInit(&g, 8) + 8 edges between neighbouring cities");
        }
        if (streq(a[1], "addEdge") && n > 3) {
            int u = parse_int(a[2], &okn), v2 = parse_int(a[3], &okn);
            if (u < 0 || u > 7 || v2 < 0 || v2 > 7 || u == v2) { ok = 0; msg = "vertices must be two different numbers from 0 to 7"; }
            else addEdge(&LG, u, v2);
        }
        else if (streq(a[1], "bfs") && n > 2 && (parse_int(a[2], &okn) < 0 || parse_int(a[2], &okn) > 7)) {
            ok = 0; msg = "start vertex must be 0 to 7";
        }
        else if (streq(a[1], "bfs") && n > 2) {
            int checks; lgN = bfs(&LG, parse_int(a[2], &okn), 0, lgOrder, lgLevel, &checks);
            { Buf *t = trace_begin(6); buf_s(t, "bfs(&g, "); buf_s(t, LGNAMES[lgOrder[0]]); buf_s(t, ") reached "); buf_i(t, lgN);
              buf_s(t, " vertices, "); buf_i(t, checks); buf_s(t, " matrix checks"); trace_end(); }
        }
        buf_s(&O, ",\"names\":["); { int i2; for (i2 = 0; i2 < 8; i2++) { if (i2) buf_c(&O, ','); j_str(&O, LGNAMES[i2]); } }
        buf_s(&O, "],\"matrix\":["); { int i2, j; for (i2 = 0; i2 < 8; i2++) { if (i2) buf_c(&O, ','); buf_c(&O, '"');
            for (j = 0; j < 8; j++) buf_c(&O, hasEdge(&LG, i2, j) ? '1' : '0'); buf_c(&O, '"'); } }
        buf_s(&O, "],\"order\":["); { int i2; for (i2 = 0; i2 < lgN; i2++) { if (i2) buf_c(&O, ','); buf_i(&O, lgOrder[i2]); } }
        buf_s(&O, "],\"levels\":["); { int i2; for (i2 = 0; i2 < lgN; i2++) { if (i2) buf_c(&O, ','); buf_i(&O, lgLevel[lgOrder[i2]]); } }
        buf_c(&O, ']');
    }
    else if (exp == 7) {
        if (labN == 0 || streq(a[1], "load")) {
            int i2, k2 = 0, step = g_ncells / 12; if (step < 1) step = 1;
            for (i2 = 0; i2 < g_ncells && k2 < 12; i2 += step) { Cell *p = cellByIndex(i2); if (p->valid) { labOrig[k2].key = p->tmax; labOrig[k2].id = i2; k2++; } }
            labN = k2; memcpy(labArr, labOrig, sizeof(Item) * (hsize)labN); labSorted = 0; labFound = -2; labCmp = 0;
            { Buf *t = trace_begin(7); buf_s(t, "load "); buf_i(t, labN); buf_s(t, " temperatures from today's grid into Item a[]"); trace_end(); }
        }
        if (streq(a[1], "quick") || streq(a[1], "merge") || streq(a[1], "insertion")) {
            memcpy(labArr, labOrig, sizeof(Item) * (hsize)labN); g_cmp = 0;
            if (streq(a[1], "quick")) quickSort(labArr, 0, labN - 1);
            else if (streq(a[1], "merge")) mergeSort(labArr, labN);
            else insertionSort(labArr, labN);
            labCmp = g_cmp; labSorted = 1; labFound = -2;
            { Buf *t = trace_begin(7); buf_s(t, a[1]); buf_s(t, "Sort(a, "); buf_i(t, labN); buf_s(t, ")  "); buf_i(t, labCmp); buf_s(t, " comparisons"); trace_end(); }
        } else if (streq(a[1], "search") && n > 2) {
            if (!labSorted) { ok = 0; msg = "sort first: binary search only works on a sorted array"; }
            else {
                float key = parse_float(a[2], &okn); int idx;
                if (!okn) { ok = 0; msg = "search key must be a number"; goto lab7done; }
                idx = binarySearch(labArr, labN, key, &labSteps);
                labFound = (idx < labN && labArr[idx].key == key) ? idx : -1;
                { Buf *t = trace_begin(7); buf_s(t, "binarySearch(a, "); buf_f1(t, key); buf_s(t, ") -> ");
                  if (labFound >= 0) { buf_s(t, "index "); buf_i(t, labFound); } else { buf_s(t, "not found (would insert at "); buf_i(t, idx); buf_c(t, ')'); }
                  buf_s(t, " in "); buf_i(t, labSteps); buf_s(t, " steps"); trace_end(); }
            }
        } else if (streq(a[1], "shuffle")) { memcpy(labArr, labOrig, sizeof(Item) * (hsize)labN); labSorted = 0; labFound = -2; }
    lab7done:
        buf_s(&O, ",\"arr\":["); { int i2; for (i2 = 0; i2 < labN; i2++) { if (i2) buf_c(&O, ','); buf_f1(&O, labArr[i2].key); } }
        buf_s(&O, "],\"sorted\":"); buf_i(&O, labSorted); buf_s(&O, ",\"cmp\":"); buf_i(&O, labCmp);
        buf_s(&O, ",\"found\":"); buf_i(&O, labFound); buf_s(&O, ",\"steps\":"); buf_i(&O, labSteps);
    }
    else if (exp == 8) {
        if (!labHashReady || streq(a[1], "reset")) { hashInit(&labHash, 11); labHashReady = 1; lastHome = lastSlot = -1; lastKey[0] = 0; trace_line(8, "hashInit(&h, 11)"); }
        if ((streq(a[1], "insert") || streq(a[1], "search") || streq(a[1], "delete")) && n > 2) {
            strcopy(lastKey, a[2], 24);
            lastHome = (int)hashKey(a[2], labHash.cap);
            { Buf *t = trace_begin(8); buf_s(t, "hash(\""); buf_s(t, a[2]); buf_s(t, "\") = djb2 % 11 = "); buf_i(t, lastHome); trace_end(); }
            if (streq(a[1], "insert")) { ok = hashInsert(&labHash, a[2], 1, &lastSlot, &lastProbes); if (!ok) msg = "table full"; lastFound = ok; }
            else if (streq(a[1], "search")) { lastFound = hashSearch(&labHash, a[2], &lastSlot, &lastProbes) >= 0; if (!lastFound) { ok = 0; msg = "not found"; } }
            else { lastFound = hashDelete(&labHash, a[2], &lastSlot, &lastProbes); if (!lastFound) { ok = 0; msg = "not found"; } }
        }
        buf_s(&O, ",\"cap\":"); buf_i(&O, labHash.cap); buf_s(&O, ",\"count\":"); buf_i(&O, labHash.count);
        buf_s(&O, ",\"slots\":["); { int i2; for (i2 = 0; i2 < labHash.cap; i2++) { Slot *s = &labHash.slots[i2]; if (i2) buf_c(&O, ',');
            buf_s(&O, "{\"state\":"); buf_i(&O, s->state); buf_s(&O, ",\"key\":"); j_str(&O, s->state == SLOT_USED ? s->key : "");
            buf_s(&O, ",\"home\":"); buf_i(&O, s->state == SLOT_USED ? (int)hashKey(s->key, labHash.cap) : -1); buf_c(&O, '}'); } }
        buf_s(&O, "],\"last\":{\"key\":"); j_str(&O, lastKey); buf_s(&O, ",\"home\":"); buf_i(&O, lastHome);
        buf_s(&O, ",\"slot\":"); buf_i(&O, lastSlot); buf_s(&O, ",\"probes\":"); buf_i(&O, lastProbes); buf_c(&O, '}');
    }
    else { ok = 0; msg = "unknown experiment"; }

    g_verbose = 0;
    if (!msg[0] && mb.len) msg = msgbuf;
    buf_s(&O, ",\"ok\":"); buf_i(&O, ok); buf_s(&O, ",\"msg\":"); j_str(&O, msg); buf_c(&O, '}');
    return O.len;
}
