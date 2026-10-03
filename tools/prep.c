/*
 * prep.c — build tool (runs once on your computer, not in the browser).
 *
 * Reads IMD Pune's yearly gridded maximum-temperature files (1° x 1°, binary,
 * 31 x 31 floats per day, 99.9 = no data) and packs the pre-monsoon season
 * (1 March – 30 June, 122 days) for land cells only into one small file,
 * public/data/season.bin, which the website's C core reads.
 *
 * Read order follows IMD's own sample C code: one float t[31][31] per day,
 * first index = latitude (7.5N .. 37.5N), second = longitude (67.5E .. 97.5E).
 *
 * season.bin layout (little-endian):
 *   "HWH1"                      4 bytes  magic
 *   int16 nyears, ndays, ncells, reserved
 *   int16 year[nyears]
 *   uint8 row, col  [ncells]     row = lat index, col = lon index
 *   int16 t10[nyears][ndays][ncells]   tenths of °C, -32768 = no data
 *
 * Build:  gcc -std=c99 -O2 -Wall -Wextra tools/prep.c -o prep
 * Run:    ./prep data/ public/data/season.bin 2015 2016 ... 2025
 */
#include <stdio.h>
#include <stdlib.h>

#define N 31
#define SEASON_DAYS 122
#define MISSING_IN 99.0f        /* IMD uses 99.9; anything >= 99 is missing */
#define MISSING_OUT (-32768)
#define MAX_YEARS 80

static float day_buf[N][N];

static int is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static void put16(FILE *f, int v) {
    unsigned short u = (unsigned short)(short)v;
    fputc(u & 0xff, f);
    fputc((u >> 8) & 0xff, f);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <grd_dir> <out_file> <year> [year ...]\n", argv[0]);
        return 1;
    }
    const char *dir = argv[1], *out_path = argv[2];
    int nyears = argc - 3;
    if (nyears > MAX_YEARS) { fprintf(stderr, "too many years\n"); return 1; }

    int years[MAX_YEARS];
    static short season[MAX_YEARS][SEASON_DAYS][N][N];
    static unsigned char land[N][N];

    for (int k = 0; k < nyears; k++) {
        years[k] = atoi(argv[3 + k]);
        char path[512];
        snprintf(path, sizeof path, "%s/Maxtemp_MaxT_%d.GRD", dir, years[k]);
        FILE *fin = fopen(path, "rb");
        if (!fin) { fprintf(stderr, "can't open %s\n", path); return 1; }

        int ndays = is_leap(years[k]) ? 366 : 365;
        int mar1 = is_leap(years[k]) ? 60 : 59;      /* 0-based day index of 1 March */
        for (int d = 0; d < ndays; d++) {
            if (fread(day_buf, sizeof day_buf, 1, fin) != 1) {
                fprintf(stderr, "%s: short read at day %d\n", path, d);
                fclose(fin);
                return 1;
            }
            for (int i = 0; i < N; i++)
                for (int j = 0; j < N; j++) {
                    float t = day_buf[i][j];
                    int valid = t < MISSING_IN && t > -60.0f;
                    if (valid) land[i][j] = 1;
                    if (d >= mar1 && d < mar1 + SEASON_DAYS) {
                        season[k][d - mar1][i][j] = valid
                            ? (short)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f))
                            : (short)MISSING_OUT;
                    }
                }
        }
        fclose(fin);
        printf("read %s\n", path);
    }

    int ncells = 0;
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            if (land[i][j]) ncells++;

    FILE *fo = fopen(out_path, "wb");
    if (!fo) { fprintf(stderr, "can't write %s\n", out_path); return 1; }
    fwrite("HWH1", 1, 4, fo);
    put16(fo, nyears); put16(fo, SEASON_DAYS); put16(fo, ncells); put16(fo, 0);
    for (int k = 0; k < nyears; k++) put16(fo, years[k]);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            if (land[i][j]) { fputc(i, fo); fputc(j, fo); }
    for (int k = 0; k < nyears; k++)
        for (int d = 0; d < SEASON_DAYS; d++)
            for (int i = 0; i < N; i++)
                for (int j = 0; j < N; j++)
                    if (land[i][j]) put16(fo, season[k][d][i][j]);
    long size = ftell(fo);
    fclose(fo);
    printf("wrote %s: %d years x %d days x %d land cells, %ld bytes\n",
           out_path, nyears, SEASON_DAYS, ncells, size);
    return 0;
}
