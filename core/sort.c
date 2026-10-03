/*
 * sort.c — Exp 7: sorting, and binary search on the sorted result.
 * Every key comparison adds 1 to g_cmp so the page can show real counts.
 */
#include "hh.h"

#define MS_MAX 2048
static Item tmp[MS_MAX];

static int less(float a, float b) { g_cmp++; return a < b; }

static void swap(Item *a, Item *b) { Item t = *a; *a = *b; *b = t; }

void quickSort(Item *a, int lo, int hi) {
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2, i = lo, j;
        float pivot;
        swap(&a[mid], &a[hi]);               /* middle element as pivot */
        pivot = a[hi].key;
        for (j = lo; j < hi; j++)
            if (less(a[j].key, pivot)) swap(&a[i++], &a[j]);
        swap(&a[i], &a[hi]);
        if (i - lo < hi - i) { quickSort(a, lo, i - 1); lo = i + 1; }   /* recurse on smaller side */
        else { quickSort(a, i + 1, hi); hi = i - 1; }
    }
}

static void mergeRec(Item *a, int lo, int hi) {
    int mid, i, j, k;
    if (hi - lo < 1) return;
    mid = lo + (hi - lo) / 2;
    mergeRec(a, lo, mid);
    mergeRec(a, mid + 1, hi);
    i = lo; j = mid + 1; k = lo;
    while (i <= mid && j <= hi) tmp[k++] = less(a[j].key, a[i].key) ? a[j++] : a[i++];
    while (i <= mid) tmp[k++] = a[i++];
    while (j <= hi) tmp[k++] = a[j++];
    for (k = lo; k <= hi; k++) a[k] = tmp[k];
}

void mergeSort(Item *a, int n) { if (n > 1 && n <= MS_MAX) mergeRec(a, 0, n - 1); }

void insertionSort(Item *a, int n) {
    int i, j;
    for (i = 1; i < n; i++) {
        Item x = a[i];
        for (j = i - 1; j >= 0 && less(x.key, a[j].key); j--) a[j + 1] = a[j];
        a[j + 1] = x;
    }
}

int binarySearch(Item *a, int n, float key, int *steps) {
    int lo = 0, hi = n, s = 0;              /* first index whose key >= target */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        s++;
        if (g_verbose) {
            Buf *t = trace_begin(7);
            buf_s(t, "binarySearch step "); buf_i(t, s); buf_s(t, ": lo="); buf_i(t, lo);
            buf_s(t, " mid="); buf_i(t, mid); buf_s(t, " hi="); buf_i(t, hi);
            buf_s(t, "  a[mid]="); buf_f1(t, a[mid].key);
            buf_s(t, a[mid].key < key ? " < key, go right" : " >= key, go left");
            trace_end();
        }
        if (less(a[mid].key, key)) lo = mid + 1; else hi = mid;
    }
    if (steps) *steps = s;
    return lo;
}

void reverseItems(Item *a, int n) { int i; for (i = 0; i < n / 2; i++) swap(&a[i], &a[n - 1 - i]); }
