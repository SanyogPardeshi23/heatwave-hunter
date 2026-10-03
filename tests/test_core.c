/* Native test runner: links the same core C files used by the website. */
#include <stdio.h>
#include <string.h>

char *hh_in(void); char *hh_out(void);
unsigned char *hh_data(void); int hh_data_cap(void);
int hh_init(int); int hh_day(int, int); int hh_select(int); int hh_rule(void);
int hh_years(void); int hh_query(void); int hh_trace(void); int hh_lab(void);
int hh_threshold(int); int hh_watch(void); int hh_state(void);

static int failures = 0;
static void show(const char *label, int n) {
    printf("=== %s (%d bytes)\n%.*s\n", label, n, n > 1500 ? 1500 : n, hh_out());
    if (n <= 0) failures++;
}
static void in(const char *s) { strcpy(hh_in(), s); }

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "public/data/season.bin";
    FILE *f = fopen(path, "rb");
    int len, n;
    if (!f) { printf("no data file\n"); return 1; }
    len = (int)fread(hh_data(), 1, (size_t)hh_data_cap(), f);
    fclose(f);
    n = hh_init(len); show("init", n > 300 ? 300 : n);
    n = hh_day(9, 88); show("day 2024-05-28", n);
    n = hh_select(100); show("select 100", n);
    in("T >= 46 && days >= 2"); n = hh_rule(); show("rule", n > 900 ? 900 : n);
    in("T >= "); n = hh_rule(); show("bad rule", n);
    n = hh_years(); show("years", n);
    in("above 45 on 28-05-2024"); n = hh_query(); show("q above", n);
    in("hottest 10"); n = hh_query(); show("q hottest", n);
    in("hottest years"); n = hh_query(); show("q years", n);
    in("find delhi"); n = hh_query(); show("q find", n);
    in("find Sri Ganganagar"); n = hh_query(); show("q find 2 words", n);
    in("spread from churu on 28-05-2024"); n = hh_query(); show("q spread", n);
    in("where T >= 46 && lat > 26"); n = hh_query(); show("q where", n);
    in("above 45 on 28-08-2024"); n = hh_query(); show("q bad date", n);
    in("insertBegin|Churu"); n = hh_watch(); show("watch add", n);
    in("deleteBefore|Jaipur"); n = hh_watch(); show("watch delBefore", n);
    n = hh_threshold(440); show("thr 44", n > 400 ? 400 : n);
    in("2|reset"); n = hh_lab(); show("lab2 reset", n);
    in("2|insertAfter|Delhi|Jaipur"); n = hh_lab(); show("lab2 insertAfter", n);
    in("2|deleteBefore|Churu"); n = hh_lab(); show("lab2 delBefore head", n);
    in("3|convert|(T + 2) * 3 >= 140 || days > 3|45|1"); n = hh_lab(); show("lab3 convert", n);
    in("3|pop"); n = hh_lab(); show("lab3 pop empty", n);
    in("4|enqueue|5"); n = hh_lab(); show("lab4 enq", n);
    in("5|reset"); n = hh_lab(); show("lab5 reset", n);
    in("5|delete|45.2"); n = hh_lab(); show("lab5 delete root", n);
    in("6|bfs|0"); n = hh_lab(); show("lab6 bfs", n);
    in("7|load"); n = hh_lab(); in("7|search|40"); n = hh_lab(); show("lab7 search unsorted", n);
    in("7|merge"); n = hh_lab(); show("lab7 merge", n);
    in("8|insert|delhi"); hh_lab(); in("8|insert|jaipur"); hh_lab(); in("8|delete|delhi"); hh_lab();
    in("8|search|jaipur"); n = hh_lab(); show("lab8", n);
    in("1|update|28.5|77.5|50"); n = hh_lab(); show("lab1", n);
    n = hh_trace(); show("trace", n);
    printf("failures: %d\n", failures);
    return failures != 0;
}
