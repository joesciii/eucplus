/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host test of the EUC+ pages (ui.c): the key and encoder handling, and what the screens put where,
 * on a 128 x 64 raster with a stand-in font (the device draws the firmware's).
 *   cc -O1 -DEUP_HOST -o ui_host_test test/ui_host_test.c ui.c eucplus.c && ./ui_host_test [PREVIEW_DIR]
 * With PREVIEW_DIR it also writes the screens as .pbm images there. */
#include <stdio.h>
#include <string.h>
#include "../eucplus.h"

volatile int32_t eup_host_seq_run = 1;
int32_t eup_host_active_track = 0;

void eup_ui_render(void *ctx, void (*f)(void *, int32_t, int32_t, int32_t, int32_t, int32_t),
                   void (*tx)(void *, int32_t, int32_t, int32_t, int32_t, const char *));
void eup_host_set_euclid(int on);
int eup_host_open(void);
int eup_host_page(void);
int eup_key(void *brain, void *ev);
int eup_enc(void *brain, void *ev);
void eup_tick(void *ctrl);

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- the raster ------------------------------------------------------------------------------ */
static unsigned char px[64][128];
static int out_of_bounds;

static void r_fill(void *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t col)
{
    int x, y;
    (void)ctx;
    if (x0 < 0 || y0 < 0 || x1 > 127 || y1 > 63 || x0 > x1 || y0 > y1)
        out_of_bounds++;
    for (y = y0 < 0 ? 0 : y0; y <= y1 && y < 64; y++)
        for (x = x0 < 0 ? 0 : x0; x <= x1 && x < 128; x++)
            px[y][x] = col < 0 ? !px[y][x] : (unsigned char)(col != 0);
}

/* a stand-in font: each character a 3 x 5 block with its middle row cut, 4 pixels a character */
static void r_text(void *ctx, int32_t font, int32_t x, int32_t ytop, int32_t flags, const char *s)
{
    int n = (int)strlen(s), w = n ? 4 * n - 1 : 0, i;
    (void)font;
    if (flags & 2)
        x -= w / 2;
    else if (flags & 4)
        x -= w;
    if (flags & 0x18)                   /* an inverted box, clipped as the firmware clips it */
        r_fill(ctx, x - 1, ytop - 1, x + w, ytop + 5 > 63 ? 63 : ytop + 5, 1);
    for (i = 0; i < n; i++) {
        if (s[i] == ' ')
            continue;
        r_fill(ctx, x + 4 * i, ytop, x + 4 * i + 2, ytop + 4, flags & 0x18 ? 0 : 1);
        r_fill(ctx, x + 4 * i + 1, ytop + 2, x + 4 * i + 1, ytop + 2, flags & 0x18 ? 1 : 0);
    }
}

static int count(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            n += px[y][x];
    return n;
}

static void render(void)
{
    memset(px, 0, sizeof px);
    eup_ui_render(0, r_fill, r_text);
}

static void save(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    int x, y;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "P1\n128 64\n");
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 128; x++)
            fputc(px[y][x] ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}

/* ---- events ---------------------------------------------------------------------------------- */
static int key(int code, int flags)
{
    int32_t ev[8] = {0};
    ev[3] = code;
    ev[4] = flags;
    return eup_key(0, ev);
}

static int enc(int e, int d)
{
    int32_t ev[8] = {0};
    ev[3] = e;
    ev[4] = d;
    return eup_enc(0, ev);
}

static void ticks(int n)
{
    unsigned char ctrl[64];
    while (n--)
        eup_tick(ctrl);
}

enum { FUNC = 17, AMP = 4, NO = 12, LEFT = 13, RIGHT = 15, PLAY = 20, SRC = 2, TRIG1 = 25, PUSH_A = 41 };
#define PRESS 1
#define WFUNC 2
#define RELEASE 0x10

static int func_amp(void)               /* FUNC + AMP, as the firmware reports it: the AMP press */
{
    int took;
    key(FUNC, PRESS);
    took = key(AMP, PRESS | WFUNC);
    key(AMP, RELEASE | WFUNC);
    key(FUNC, 0);
    return took;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    struct eup_track *t0 = &eup_cfg.t[0];
    int x;

    /* ---- opening: only from the stock Euclidean page, never switching EUC+ on ---- */
    eup_host_set_euclid(0);
    CHECK(func_amp() == 0 && !eup_host_open());             /* elsewhere: stock's */
    eup_host_set_euclid(1);
    key(FUNC, PRESS);
    CHECK(key(AMP, PRESS | WFUNC) == 1 && eup_host_open());
    CHECK(key(AMP, RELEASE | WFUNC) == 1);                   /* its release is taken too */
    CHECK(key(FUNC, 0) == 0);                                /* FUNC's is not */
    CHECK(t0->on == 0);                                      /* opening leaves ON as it was */
    CHECK(eup_host_page() == 0);

    /* ---- the NOTES page closed (off) ---- */
    render();
    save(dir, "notes-off");
    CHECK(out_of_bounds == 0);
    CHECK(count(0, 0, 15, 9) == 0);                          /* the stock pattern box is left alone */
    CHECK(count(18, 0, 126, 0) == 109 && count(18, 9, 126, 9) == 109);  /* the title bar, white */
    CHECK(px[1][17] && px[1][127] && !px[0][17] && !px[0][127]);        /* its cut corners */
    CHECK(count(20, 2, 100, 7) < 6 * 80);                    /* its text, dark on it */
    CHECK(px[13][7] && px[36][7] && px[24][0] && px[24][14]);           /* the page box */
    CHECK(!px[13][0] && !px[36][14]);                        /* corners cut */
    CHECK(count(4, 42, 10, 54) < 5);                         /* the ON switch: off */
    for (x = 0; x < 4; x++) {
        CHECK(px[21 - 8][33 + 26 * x] || x == 2);            /* knobs: PTCH RNGE PLEN; PHRS is a list */
        CHECK(count(33 + 26 * x - 10, 32, 33 + 26 * x + 10, 36) > 10);  /* every label */
    }
    CHECK(count(23, 39, 43, 63) == 0);                       /* slot E (HARM, later): empty */

    /* ---- encoders ---- */
    CHECK(enc(8, 1) == 1 && t0->on == 1);                    /* H: ON */
    render();
    save(dir, "notes-on");
    CHECK(count(4, 42, 10, 54) == 7 * 13);                   /* the switch filled */
    CHECK(enc(1, 30) == 1 && t0->ptch == 24);                /* PTCH holds its range */
    CHECK(enc(1, -100) == 1 && t0->ptch == -24);
    CHECK(enc(1, 26) == 1 && t0->ptch == 2);
    render();
    save(dir, "notes-turn");
    CHECK(px[31][33] && px[37][33]);                         /* the value boxed in the label's place */
    ticks(46);                                               /* 1.5 s later the label is back */
    render();
    CHECK(!px[31][33] && !px[37][33]);
    CHECK(enc(9, 3) == 0);                                   /* LEVEL: stock */
    CHECK(enc(6, 2) == 1 && t0->scl == 2);                   /* SCL: IONI */
    CHECK(enc(7, 99) == 1 && t0->root == ROOT_COUNT - 1);    /* ROOT: B, the last */
    enc(6, -2);
    enc(7, -99);

    /* ---- keys while open ---- */
    CHECK(key(RIGHT, PRESS) == 1 && eup_host_page() == 1);   /* RANDOM */
    render();
    save(dir, "random");
    CHECK(out_of_bounds == 0);
    CHECK(count(18, 39, 127, 63) == 0);                      /* the second row is unused */
    CHECK(count(18, 11, 127, 37) > 100);                     /* VAR SEED LOCK RAMT */
    CHECK(count(4, 42, 10, 54) == 7 * 13);                   /* ON shows on RANDOM too */
    CHECK(enc(3, -1) == 1 && t0->lock == 0);                 /* LOCK off */
    CHECK(enc(2, 300) == 1 && t0->seed == 255);
    CHECK(key(RIGHT, PRESS) == 1 && eup_host_page() == 0);   /* wraps */
    CHECK(key(LEFT, PRESS) == 1 && eup_host_page() == 1);
    CHECK(key(LEFT, RELEASE) == 1);
    CHECK(key(PLAY, PRESS) == 0 && eup_host_open());         /* transport: stock, EUC+ stays */
    CHECK(key(TRIG1, PRESS) == 0 && eup_host_open());
    CHECK(key(PUSH_A, PRESS) == 1);                          /* encoder pushes: EUC+'s */
    eup_host_active_track = 3;                               /* TRK + T4: the pages follow */
    CHECK(enc(3, 1) == 1 && eup_cfg.t[3].lock == 1 && t0->lock == 0);
    eup_host_active_track = 0;
    CHECK(key(NO, PRESS) == 1 && !eup_host_open());          /* NO: back to the stock page */
    CHECK(key(NO, RELEASE) == 1);
    CHECK(t0->on == 1);                                      /* closing leaves ON as it was */
    CHECK(enc(1, 1) == 0 && t0->ptch == 2);                  /* closed: encoders are stock's */

    eup_host_set_euclid(1);
    CHECK(func_amp() == 1 && eup_host_open());               /* again, and FUNC + AMP closes */
    CHECK(func_amp() == 1 && !eup_host_open());
    eup_host_set_euclid(1);
    func_amp();
    CHECK(key(SRC, PRESS) == 0 && !eup_host_open());         /* a page key: closes, stock's */
    CHECK(t0->on == 1);

    printf("ui host test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
