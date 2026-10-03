/*
 * grid.c — Exp 1: arrays, array of structures, pointers.
 * The whole day's map is   Cell grid[31][31]   — one struct per 1° grid point.
 * readGrid() fills it from the packed IMD data; getCell() returns a pointer.
 */
#include "hh.h"

#define DATA_CAP (1200 * 1024)

Cell grid[GN][GN];
int  g_nyears, g_ndays, g_ncells;
int  g_years[MAX_YEARS];
int  g_cellRow[MAX_CELLS], g_cellCol[MAX_CELLS];
int  g_cellAt[GN][GN];

static unsigned char data[DATA_CAP];
static int data_len = 0, data_off = 0;

unsigned char *data_buffer(void) { return data; }
int data_capacity(void) { return DATA_CAP; }

static int rd16(int off) {
    int v = data[off] | (data[off + 1] << 8);
    return v >= 32768 ? v - 65536 : v;
}

int data_parse(int len) {
    int off, i, r, c;
    data_len = len;
    if (len < 12 || data[0] != 'H' || data[1] != 'W' || data[2] != 'H' || data[3] != '1') return 0;
    g_nyears = rd16(4); g_ndays = rd16(6); g_ncells = rd16(8);
    if (g_nyears < 1 || g_nyears > MAX_YEARS || g_ndays != SEASON_DAYS ||
        g_ncells < 1 || g_ncells > MAX_CELLS) return 0;
    off = 12;
    for (i = 0; i < g_nyears; i++, off += 2) g_years[i] = rd16(off);
    for (r = 0; r < GN; r++) for (c = 0; c < GN; c++) g_cellAt[r][c] = -1;
    for (i = 0; i < g_ncells; i++, off += 2) {
        g_cellRow[i] = data[off]; g_cellCol[i] = data[off + 1];
        if (g_cellRow[i] >= GN || g_cellCol[i] >= GN) return 0;
        g_cellAt[g_cellRow[i]][g_cellCol[i]] = i;
    }
    data_off = off;
    if (data_off + 2L * g_nyears * g_ndays * g_ncells > len) return 0;
    for (r = 0; r < GN; r++)
        for (c = 0; c < GN; c++) {
            Cell *p = &grid[r][c];
            p->row = r; p->col = c;
            p->lat = 7.5f + (float)r; p->lon = 67.5f + (float)c;
            p->index = g_cellAt[r][c];
            p->valid = 0; p->tmax = 0;
        }
    return 1;
}

float cell_value(int yi, int d, int cell, int *valid) {
    int v = rd16(data_off + 2 * ((yi * g_ndays + d) * g_ncells + cell));
    if (v == NO_DATA) { if (valid) *valid = 0; return 0; }
    if (valid) *valid = 1;
    return (float)v / 10.0f;
}

void readGrid(int yi, int d) {
    int r, c, nvalid = 0;
    for (r = 0; r < GN; r++)
        for (c = 0; c < GN; c++) {
            Cell *p = &grid[r][c];
            if (p->index < 0) { p->valid = 0; continue; }
            p->tmax = cell_value(yi, d, p->index, &p->valid);
            nvalid += p->valid;
        }
    {
        Buf *t = trace_begin(1);
        buf_s(t, "readGrid(season.bin, "); buf_i(t, g_years[yi]);
        buf_s(t, ", day "); buf_i(t, d + 1); buf_s(t, ")  -> grid[31][31], ");
        buf_i(t, nvalid); buf_s(t, " valid cells  &grid[0][0]=");
        buf_hex(t, (unsigned long)&grid[0][0]);
        trace_end();
    }
}

Cell *getCell(float lat, float lon) {
    int r = (int)(lat - 7.5f + 0.5f), c = (int)(lon - 67.5f + 0.5f);
    if (lat < 7.0f || lon < 67.0f || r < 0 || r >= GN || c < 0 || c >= GN) return 0;
    return &grid[r][c];
}

Cell *cellByIndex(int idx) {
    if (idx < 0 || idx >= g_ncells) return 0;
    return &grid[g_cellRow[idx]][g_cellCol[idx]];
}

int updateCell(Cell *c, float tmax) {
    if (!c || c->index < 0) return 0;
    c->tmax = tmax; c->valid = 1;
    return 1;
}
