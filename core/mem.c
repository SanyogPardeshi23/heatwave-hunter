/* mem.c — the small pieces of libc we need, written by hand. */
#include "hh.h"

void *memset(void *d, int c, hsize n) {
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, hsize n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, hsize n) {
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

int streqi(const char *a, const char *b) {
    while (*a && to_lower(*a) == to_lower(*b)) { a++; b++; }
    return to_lower(*a) == to_lower(*b);
}

void strcopy(char *dst, const char *src, int cap) {
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

int is_digit(char c) { return c >= '0' && c <= '9'; }
int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

float parse_float(const char *s, int *ok) {
    float v = 0, frac = 0.1f; int neg = 0, any = 0;
    while (is_space(*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (is_digit(*s)) { v = v * 10 + (float)(*s - '0'); s++; any = 1; }
    if (*s == '.') { s++; while (is_digit(*s)) { v += (float)(*s - '0') * frac; frac *= 0.1f; s++; any = 1; } }
    while (is_space(*s)) s++;
    if (ok) *ok = any && *s == 0;
    return neg ? -v : v;
}

int parse_int(const char *s, int *ok) {
    int v = 0, neg = 0, any = 0;
    while (is_space(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    while (is_digit(*s)) { v = v * 10 + (*s - '0'); s++; any = 1; }
    while (is_space(*s)) s++;
    if (ok) *ok = any && *s == 0;
    return neg ? -v : v;
}

/* ---------- Buf: bounded text builder ---------- */
void buf_init(Buf *b, char *mem, int cap) { b->buf = mem; b->cap = cap; b->len = 0; if (cap) mem[0] = 0; }

void buf_c(Buf *b, char c) {
    if (b->len < b->cap - 1) { b->buf[b->len++] = c; b->buf[b->len] = 0; }
}

void buf_s(Buf *b, const char *s) { while (*s) buf_c(b, *s++); }

void buf_i(Buf *b, long v) {
    char tmp[24]; int n = 0;
    unsigned long u;
    if (v < 0) { buf_c(b, '-'); u = (unsigned long)(-(v + 1)) + 1; } else u = (unsigned long)v;
    do { tmp[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) buf_c(b, tmp[--n]);
}

void buf_f1(Buf *b, float v) {
    long t;
    if (v < 0) { buf_c(b, '-'); v = -v; }
    t = (long)(v * 10.0f + 0.5f);
    buf_i(b, t / 10);
    buf_c(b, '.');
    buf_c(b, (char)('0' + t % 10));
}

void buf_hex(Buf *b, unsigned long v) {
    const char *h = "0123456789abcdef"; char tmp[16]; int n = 0, i;
    do { tmp[n++] = h[v & 15]; v >>= 4; } while (v);
    buf_s(b, "0x");
    for (i = n; i < 8; i++) buf_c(b, '0');
    while (n) buf_c(b, tmp[--n]);
}

/* ---------- JSON helpers ---------- */
void j_str(Buf *b, const char *s) {
    buf_c(b, '"');
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') { buf_c(b, '\\'); buf_c(b, c); }
        else if (c == '\n') buf_s(b, "\\n");
        else if ((unsigned char)c < 32) buf_c(b, ' ');
        else buf_c(b, c);
    }
    buf_c(b, '"');
}

void j_key(Buf *b, const char *k) { j_str(b, k); buf_c(b, ':'); }
