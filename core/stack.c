/*
 * stack.c — Exp 3: stack built on a singly linked list; infix -> postfix
 * conversion and postfix evaluation. Powers the alert rule, e.g.
 *     T >= 45 && days >= 2      ->      T 45 >= days 2 >= &&
 */
#include "hh.h"

#define SPOOL 256
static SNode spool[SPOOL];
static SNode *sfree = 0;
static int sready = 0;

static SNode *snodeAlloc(void) {
    SNode *n;
    if (!sready) {
        int i;
        for (i = 0; i < SPOOL - 1; i++) spool[i].next = &spool[i + 1];
        spool[SPOOL - 1].next = 0; sfree = &spool[0]; sready = 1;
    }
    if (!sfree) return 0;
    n = sfree; sfree = n->next; n->next = 0;
    return n;
}
static void snodeFree(SNode *n) { n->next = sfree; sfree = n; }

void createStack(Stack *s) { s->top = 0; s->size = 0; }
int  isEmpty(Stack *s) { return s->top == 0; }

void push(Stack *s, Token t) {
    SNode *n = snodeAlloc();
    if (!n) { trace_line(3, "push(): stack pool full"); return; }
    n->tok = t; n->next = s->top; s->top = n; s->size++;
}

Token pop(Stack *s) {
    Token t; SNode *n = s->top;
    memset(&t, 0, sizeof t);
    if (!n) return t;                 /* underflow: empty token */
    t = n->tok; s->top = n->next; s->size--; snodeFree(n);
    return t;
}

Token peek(Stack *s) {
    Token t;
    memset(&t, 0, sizeof t);
    if (s->top) t = s->top->tok;
    return t;
}

void pushVal(Stack *s, float v) {
    SNode *n = snodeAlloc();
    if (!n) return;
    n->val = v; n->next = s->top; s->top = n; s->size++;
}

float popVal(Stack *s) {
    float v; SNode *n = s->top;
    if (!n) return 0;
    v = n->val; s->top = n->next; s->size--; snodeFree(n);
    return v;
}

void clearStack(Stack *s) { while (s->top) { SNode *n = s->top; s->top = n->next; snodeFree(n); } s->size = 0; }

/* ---------- tokenizer ---------- */
static int prec(const char *op) {
    if (streq(op, "||")) return 1;
    if (streq(op, "&&")) return 2;
    if (streq(op, "==") || streq(op, "!=")) return 3;
    if (streq(op, ">=") || streq(op, "<=") || streq(op, ">") || streq(op, "<")) return 4;
    if (streq(op, "+") || streq(op, "-")) return 5;
    if (streq(op, "*") || streq(op, "/")) return 6;
    return 0;
}

int tokenize(const char *s, Token *out, int max, char *err) {
    int n = 0;
    err[0] = 0;
    while (*s) {
        Token t; memset(&t, 0, sizeof t);
        if (is_space(*s)) { s++; continue; }
        if (n >= max) { strcopy(err, "expression too long", 64); return -1; }
        if (is_digit(*s) || (*s == '.' && is_digit(s[1]))) {
            char num[16]; int k = 0, ok;
            while ((is_digit(*s) || *s == '.') && k < 15) num[k++] = *s++;
            num[k] = 0;
            t.type = TOK_NUM; t.num = parse_float(num, &ok); strcopy(t.text, num, 8);
            if (!ok) { strcopy(err, "bad number", 64); return -1; }
        } else if (is_alpha(*s)) {
            char w[16]; int k = 0;
            while ((is_alpha(*s) || is_digit(*s)) && k < 15) w[k++] = *s++;
            w[k] = 0;
            if (streqi(w, "t") || streqi(w, "temp")) strcopy(t.text, "T", 8);
            else if (streqi(w, "days")) strcopy(t.text, "days", 8);
            else if (streqi(w, "lat")) strcopy(t.text, "lat", 8);
            else if (streqi(w, "lon")) strcopy(t.text, "lon", 8);
            else if (streqi(w, "and")) { t.type = TOK_OP; strcopy(t.text, "&&", 8); out[n++] = t; continue; }
            else if (streqi(w, "or"))  { t.type = TOK_OP; strcopy(t.text, "||", 8); out[n++] = t; continue; }
            else { strcopy(err, "unknown word (use T, days, lat, lon)", 64); return -1; }
            t.type = TOK_VAR;
        } else if (*s == '(') { t.type = TOK_LP; strcopy(t.text, "(", 8); s++; }
        else if (*s == ')') { t.type = TOK_RP; strcopy(t.text, ")", 8); s++; }
        else {
            char two[3]; two[0] = s[0]; two[1] = s[1]; two[2] = 0;
            if (s[1] && prec(two)) { t.type = TOK_OP; strcopy(t.text, two, 8); s += 2; }
            else { char one[2]; one[0] = s[0]; one[1] = 0;
                if (prec(one)) { t.type = TOK_OP; strcopy(t.text, one, 8); s++; }
                else { strcopy(err, "unexpected character", 64); return -1; } }
        }
        out[n++] = t;
    }
    return n;
}

static void stackText(Stack *s, Buf *b) {
    /* bottom..top order for display */
    Token items[MAX_TOK]; int n = 0, i; SNode *p;
    for (p = s->top; p && n < MAX_TOK; p = p->next) items[n++] = p->tok;
    for (i = n - 1; i >= 0; i--) { buf_s(b, items[i].text); if (i) buf_c(b, ' '); }
}

static void outText(Token *o, int n, Buf *b) {
    int i;
    for (i = 0; i < n; i++) { buf_s(b, o[i].text); if (i < n - 1) buf_c(b, ' '); }
}

static void step(Buf *steps, int *first, Token *tok, const char *action, Stack *s, Token *o, int on) {
    char tmp[200]; Buf b;
    if (!steps) return;
    if (!*first) buf_c(steps, ',');
    *first = 0;
    buf_s(steps, "{\"tok\":"); j_str(steps, tok ? tok->text : "end");
    buf_s(steps, ",\"action\":"); j_str(steps, action);
    buf_init(&b, tmp, sizeof tmp); stackText(s, &b);
    buf_s(steps, ",\"stack\":"); j_str(steps, tmp);
    buf_init(&b, tmp, sizeof tmp); outText(o, on, &b);
    buf_s(steps, ",\"out\":"); j_str(steps, tmp);
    buf_c(steps, '}');
}

/* Infix -> postfix, the algorithm from the Exp 3 write-up. Returns length or -1. */
int toPostfix(Token *in, int n, Token *out, Buf *steps) {
    Stack s; int i, on = 0, first = 1, pushes = 0, pops = 0;
    createStack(&s);
    if (steps) buf_c(steps, '[');
    for (i = 0; i < n; i++) {
        Token t = in[i];
        if (t.type == TOK_NUM || t.type == TOK_VAR) {
            out[on++] = t; step(steps, &first, &t, "operand -> output", &s, out, on);
        } else if (t.type == TOK_LP) {
            push(&s, t); pushes++; step(steps, &first, &t, "push (", &s, out, on);
        } else if (t.type == TOK_RP) {
            while (!isEmpty(&s) && peek(&s).type != TOK_LP) { out[on++] = pop(&s); pops++; }
            if (isEmpty(&s)) { clearStack(&s); if (steps) buf_c(steps, ']'); return -1; }
            pop(&s); pops++;
            step(steps, &first, &t, "pop until (", &s, out, on);
        } else {
            while (!isEmpty(&s) && peek(&s).type == TOK_OP && prec(peek(&s).text) >= prec(t.text)) {
                out[on++] = pop(&s); pops++;
            }
            push(&s, t); pushes++;
            step(steps, &first, &t, "pop higher/equal, push op", &s, out, on);
        }
    }
    while (!isEmpty(&s)) {
        Token t = pop(&s); pops++;
        if (t.type == TOK_LP) { clearStack(&s); if (steps) buf_c(steps, ']'); return -1; }
        out[on++] = t;
    }
    step(steps, &first, 0, "pop everything left", &s, out, on);
    if (steps) buf_c(steps, ']');
    {
        Buf *tr = trace_begin(3);
        buf_s(tr, "toPostfix("); buf_i(tr, n); buf_s(tr, " tokens)  push x"); buf_i(tr, pushes);
        buf_s(tr, ", pop x"); buf_i(tr, pops); buf_s(tr, "  -> ");
        outText(out, on, tr);
        trace_end();
    }
    return on;
}

void postfixText(Token *pf, int n, Buf *b) { outText(pf, n, b); }

float evalPostfix(Token *pf, int n, float T, float days, float lat, float lon, int *ok) {
    Stack s; int i; float r;
    createStack(&s);
    *ok = 1;
    for (i = 0; i < n; i++) {
        Token t = pf[i];
        if (t.type == TOK_NUM) pushVal(&s, t.num);
        else if (t.type == TOK_VAR) {
            float v = streq(t.text, "T") ? T : streq(t.text, "days") ? days : streq(t.text, "lat") ? lat : lon;
            pushVal(&s, v);
        } else if (t.type == TOK_OP) {
            float b, a;
            if (s.size < 2) { *ok = 0; clearStack(&s); return 0; }
            b = popVal(&s); a = popVal(&s);
            if (streq(t.text, "+")) pushVal(&s, a + b);
            else if (streq(t.text, "-")) pushVal(&s, a - b);
            else if (streq(t.text, "*")) pushVal(&s, a * b);
            else if (streq(t.text, "/")) pushVal(&s, b != 0 ? a / b : 0);
            else if (streq(t.text, ">=")) pushVal(&s, a >= b);
            else if (streq(t.text, "<=")) pushVal(&s, a <= b);
            else if (streq(t.text, ">")) pushVal(&s, a > b);
            else if (streq(t.text, "<")) pushVal(&s, a < b);
            else if (streq(t.text, "==")) pushVal(&s, a == b);
            else if (streq(t.text, "!=")) pushVal(&s, a != b);
            else if (streq(t.text, "&&")) pushVal(&s, (a != 0) && (b != 0));
            else if (streq(t.text, "||")) pushVal(&s, (a != 0) || (b != 0));
        }
    }
    if (s.size != 1) { *ok = 0; clearStack(&s); return 0; }
    r = popVal(&s);
    return r;
}
