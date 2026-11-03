/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+ v0.2: the EUC+ pages (Digitakt II OS 1.17, core-dt2 1.0's ev_tick / ev_draw / ev_key / ev_enc).
 *
 * Open them from the stock SEQUENCER (Euclidean) page with a second FUNC + AMP. FUNC + AMP again,
 * or NO, goes back to it. LEFT / RIGHT change page; encoders A-H edit the active track's settings.
 * A page key (SRC, FLTR, AMP, FX, MOD) or any other menu combination closes EUC+ and does its
 * stock job. Transport, TRK, the trig keys and FUNC keep working underneath.
 *
 * ON (NOTES, encoder H) is EUC+'s own per-track switch. Opening or closing the pages never
 * changes it, and every page shows it in its title bar.
 *
 * These handlers run on the UI task (not at interrupt level), so they may call the firmware's
 * drawing routines and its active-track getters (docs/RESEARCH-LOG.md, "v0.2: the UI"). */
#include <stdint.h>
#include "eucplus.h"

/* ---- the firmware (OS 1.17; docs/RESEARCH-LOG.md) ---------------------------------------- */
typedef void (*fillrect_t)(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
typedef int32_t (*drawstr_t)(void *bmp, const void *font, int32_t x, int32_t y, int32_t maxlen,
                             const char *s);
/* the firmware returns pointers in d0: these say uint32_t so that the compiler reads d0, not a0 */
typedef uint32_t (*getp_t)(uint32_t);
typedef uint32_t (*get0_t)(void);
typedef int32_t (*geti_t)(uint32_t);
#define FILLRECT        ((fillrect_t)0x401131ae)    /* colour 0 clears, 1 sets, -1 inverts */
#define DRAWSTR         ((drawstr_t)0x40113d60)     /* ORs the text in; returns its width */
#define FONT_LABEL      ((const void *)0x402aa570)  /* the 5-pixel capitals of the stock labels */
#define FONT_TITLE      ((const void *)0x402a9be0)  /* 6 pixels, upper and lower case */
#define SEQ_CTX         ((get0_t)0x40198b7e)        /* the sequencer context (made at boot) */
#define SEQ_TRACKSEL    ((getp_t)0x40041d18)        /* its track selection: context + 48 */
#define ACTIVE_TRACK    ((geti_t)0x4004abb8)        /* the selection's active track, 0-15 */

/* A Bitmap: +4 width, +8 height, +12 longs per column, +16 the pixels: column x, row y (0 = the
 * bottom row) is bit 31 - y % 32 of long x * stride + y / 32. */
#define BMP_W(b)        (((uint32_t *)(b))[1])
#define BMP_H(b)        (((uint32_t *)(b))[2])
#define BMP_STRIDE(b)   (((uint32_t *)(b))[3])
#define BMP_DATA(b)     (((uint32_t **)(b))[4])

/* key ids (ev + 12) and flags (ev + 16) */
enum { K_SRC = 2, K_FLTR, K_AMP, K_FX, K_MOD, K_YES = 10, K_UP, K_NO, K_LEFT, K_DOWN, K_RIGHT,
       K_TRK, K_FUNC, K_KEYBOARD, K_RECORD, K_PLAY, K_STOP, K_TRIG1 = 25, K_TRIG16 = 40,
       K_PUSH_A = 41, K_PUSH_H = 48, K_MAX = 64 };
#define KF_PRESS    1
#define KF_FUNC     2
#define KF_REPEAT   8
#define KF_RELEASE  0x10

/* The stock SEQUENCER page: a signature of its two rows of parameter labels (PL1 PL2 LEN EUC,
 * RO1 RO2 TRO OP: rows 27-31 and 0-4, columns 18-127). They are the same on audio and MIDI tracks
 * and with EUC on or off; the title bar is not used, since it shows the last parameter edited
 * ("Pulses Gen 1: 5"). Measured in the emulator (docs/RESEARCH-LOG.md). */
#define SIG_X0      18
#define SIG_X1      127
#define EUCLID_PAGE_SIG 0xf6f7e0f0u

/* ---- the pages ---------------------------------------------------------------------------- */
enum { P_NUM, P_SNUM, P_LIST, P_NONE };
struct par {
    const char *label;
    uint8_t off;                        /* in struct eup_track */
    uint8_t kind;
    int16_t lo, hi;
    const char *const *names;           /* P_LIST */
};

#define OFF(f)  ((uint8_t)__builtin_offsetof(struct eup_track, f))
static const char *const n_off[2] = { "OFF", "ON" };
static const char *const n_phrs[PH_COUNT] = { "RAND", "SAW", "TRI", "SINE", "PULS", "CAD1", "CAD2",
                                              "CAD3", "CAD4" };
static const char *const n_scl[SCL_COUNT] = {
    "FLW", "CHRM", "IONI", "DORI", "PHRY", "LYDI", "MIXO", "AEOL", "LOCR", "PMIN", "PMAJ",
    "MMIN", "HMIN", "WHOL", "BLUE", "CMIN", "PERS", "IWAT", "INSN", "HIRA", "PELO", "PDOM",
    "WHDM", "HWDM", "SPAN", "MLOC", "SLOC", "DOB2", "LAUG", "LDOM", "DHMJ", "L#26", "UPHR",
    "HUNM", "ORNT", "I#25", "LB37",
};
static const char *const n_root[ROOT_COUNT] = { "FLW", "C", "C#", "D", "D#", "E", "F", "F#", "G",
                                                "G#", "A", "A#", "B" };

#define PAGES 2
static const char *const page_name[PAGES] = { "NOTES", "RANDOM" };
static const struct par pages[PAGES][8] = {
    {
        { "PTCH", OFF(ptch), P_SNUM, -24, 24, 0 },
        { "RNGE", OFF(rnge), P_NUM, 0, 24, 0 },
        { "PHRS", OFF(phrs), P_LIST, 0, PH_COUNT - 1, n_phrs },
        { "PLEN", OFF(plen), P_NUM, 1, 64, 0 },
        { 0, 0, P_NONE, 0, 0, 0 },      /* HARM (v0.4) */
        { "SCL", OFF(scl), P_LIST, 0, SCL_COUNT - 1, n_scl },
        { "ROOT", OFF(root), P_LIST, 0, ROOT_COUNT - 1, n_root },
        { "ON", OFF(on), P_LIST, 0, 1, n_off },
    },
    {
        { "VAR", OFF(var), P_NUM, 0, 127, 0 },
        { "SEED", OFF(seed), P_NUM, 0, 255, 0 },
        { "LOCK", OFF(lock), P_LIST, 0, 1, n_off },
        { "RAMT", OFF(ramt), P_NUM, 0, 24, 0 },
        { 0, 0, P_NONE, 0, 0, 0 },
        { 0, 0, P_NONE, 0, 0, 0 },
        { 0, 0, P_NONE, 0, 0, 0 },
        { 0, 0, P_NONE, 0, 0, 0 },
    },
};

/* ---- state -------------------------------------------------------------------------------- */
struct eup_ui {
    uint8_t open;               /* the EUC+ pages are on screen */
    uint8_t page;
    uint8_t dirty;              /* draw again at the next tick */
    uint8_t euclid;             /* the last frame was the stock SEQUENCER page */
    uint32_t sig;               /* the last frame's page signature */
    uint32_t opens;             /* how many times EUC+ opened (for the tests) */
    uint8_t taken[K_MAX];       /* keys whose press EUC+ took: it takes their release too */
};
struct eup_ui eup_ui;

static int32_t active_track(void)
{
    uint32_t ctx = SEQ_CTX();
    int32_t t = ctx ? ACTIVE_TRACK(SEQ_TRACKSEL(ctx)) : 0;
    return t < 0 ? 0 : t >= TRACKS ? TRACKS - 1 : t;
}

static uint32_t page_sig(void *bmp)
{
    const uint32_t *d = BMP_DATA(bmp);
    uint32_t st = BMP_STRIDE(bmp), h = 5381, w;
    int32_t x;
    if (!d || BMP_W(bmp) != 128 || BMP_H(bmp) != 64 || st != 2)
        return 0;
    for (x = SIG_X0; x <= SIG_X1; x++) {
        w = d[x * st];                                      /* rows 0-31 */
        h = (h * 33) ^ ((w >> 27) | (w & 0x1f) << 5);       /* rows 0-4 and 27-31 */
    }
    return h;
}

/* ---- drawing ------------------------------------------------------------------------------ */
static uint32_t scratch_px[128];
static uint32_t scratch[8];             /* a 128 x 32 Bitmap, to measure text */

static int32_t text_w(const void *font, const char *s)
{
    return DRAWSTR(scratch, font, 0, 8, 16, s);
}

static void text_c(void *bmp, const void *font, int32_t cx, int32_t y, const char *s)
{
    DRAWSTR(bmp, font, cx - text_w(font, s) / 2, y, 16, s);
}

static const char *num(char *buf, int32_t v, int sign)
{
    char tmp[8];
    int n = 0, i = 0;
    if (v < 0) {
        buf[i++] = '-';
        v = -v;
    } else if (sign && v > 0) {
        buf[i++] = '+';
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n)
        buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}

static int32_t par_get(const struct eup_track *c, const struct par *p)
{
    const uint8_t *b = (const uint8_t *)c + p->off;
    return p->kind == P_SNUM ? (int32_t)(int8_t)*b : (int32_t)*b;
}

static void draw(void *bmp)
{
    int32_t t = active_track(), i;
    const struct eup_track *c = &eup_cfg.t[t];
    char buf[12];

    /* the frame: a white title bar (EUC+, the page, the track, ON / OFF), then 2 rows of 4 */
    FILLRECT(bmp, 0, 0, 127, 63, 0);
    DRAWSTR(bmp, FONT_TITLE, 3, 57, 16, "EUC+");
    DRAWSTR(bmp, FONT_LABEL, 30, 57, 16, page_name[eup_ui.page]);
    buf[0] = 'T';
    num(buf + 1, t + 1, 0);
    DRAWSTR(bmp, FONT_LABEL, 84, 57, 4, buf);
    DRAWSTR(bmp, FONT_LABEL, c->on ? 108 : 105, 57, 4, c->on ? "ON" : "OFF");
    if (c->on)
        FILLRECT(bmp, 102, 56, 125, 62, -1);            /* ON: a black box in the bar */
    FILLRECT(bmp, 0, 55, 127, 63, -1);
    for (i = 0; i < PAGES; i++)                         /* the page dots */
        FILLRECT(bmp, 64 + 4 * i, 49, 65 + 4 * i, 50, i == eup_ui.page ? 1 : -1);

    for (i = 0; i < 8; i++) {
        const struct par *p = &pages[eup_ui.page][i];
        int32_t cx = 16 + 32 * (i & 3), yv = i < 4 ? 40 : 15, v;
        const char *s;
        if (p->kind == P_NONE) {
            FILLRECT(bmp, cx - 3, yv - 6, cx + 2, yv - 6, 1);   /* an unused slot: a dash */
            continue;
        }
        v = par_get(c, p);
        s = p->kind == P_LIST ? p->names[v < p->lo ? p->lo : v > p->hi ? p->hi : v] :
                                num(buf, v, p->kind == P_SNUM);
        text_c(bmp, FONT_LABEL, cx, yv, s);
        text_c(bmp, FONT_LABEL, cx, yv - 12, p->label);
        if (p->off == OFF(on) && c->on)
            FILLRECT(bmp, cx - 11, yv - 2, cx + 10, yv + 6, -1);  /* ON: shown inverted */
    }
    FILLRECT(bmp, 2, 26, 125, 26, 1);                   /* between the rows */
}

/* ---- the events --------------------------------------------------------------------------- */
void eup_tick(void *ctrl)
{
    if (EUP_SEQ_RUN == 0)
        eup_transport_stopped();
    if (eup_ui.dirty) {
        eup_ui.dirty = 0;
        *((volatile uint8_t *)ctrl + 0x20) = 1;
    }
}

void eup_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    eup_ui.sig = page_sig(bmp);
    eup_ui.euclid = eup_ui.sig == EUCLID_PAGE_SIG;
    if (!eup_ui.open)
        return;
    if (!scratch[0]) {                  /* the measuring Bitmap: a copy of the frame's header */
        int i;
        for (i = 0; i < 8; i++)
            scratch[i] = ((uint32_t *)bmp)[i];
        scratch[1] = 128;
        scratch[2] = 32;
        scratch[3] = 1;
        scratch[4] = (uint32_t)scratch_px;
    }
    draw(bmp);
}

static void ui_open(int open)
{
    eup_ui.open = (uint8_t)open;
    eup_ui.dirty = 1;
    if (open)
        eup_ui.opens++;
}

static int take(int key)
{
    if (key >= 0 && key < K_MAX)
        eup_ui.taken[key] = 1;
    return 1;
}

int eup_key(void *brain, void *ev)
{
    int32_t key = *(const int32_t *)((const uint8_t *)ev + 12);
    uint32_t fl = *(const uint32_t *)((const uint8_t *)ev + 16);
    (void)brain;

    if (fl & KF_RELEASE) {              /* the release of a key EUC+ took is EUC+'s too */
        if (key >= 0 && key < K_MAX && eup_ui.taken[key]) {
            eup_ui.taken[key] = 0;
            return 1;
        }
        return 0;
    }
    if (!(fl & (KF_PRESS | KF_REPEAT)))
        return 0;

    if (!eup_ui.open) {
        /* a second FUNC + AMP while the stock SEQUENCER page is on screen opens EUC+ (stock does
         * nothing with it there) */
        if (key == K_AMP && (fl & KF_FUNC) && !(fl & KF_REPEAT) && eup_ui.euclid) {
            eup_ui.page = 0;
            ui_open(1);
            return take(key);
        }
        if (key != K_FUNC) {            /* the page may change: wait for its next frame */
            eup_ui.euclid = 0;
            eup_ui.dirty = 1;
        }
        return 0;
    }

    switch (key) {
    case K_AMP:
        if (fl & KF_FUNC) {             /* FUNC + AMP: back to the stock SEQUENCER page */
            if (!(fl & KF_REPEAT))
                ui_open(0);
            return take(key);
        }
        ui_open(0);                     /* AMP: close, and the stock AMP page opens */
        return 0;
    case K_NO:
        ui_open(0);
        return take(key);
    case K_LEFT:
    case K_RIGHT:
        eup_ui.page = (uint8_t)((eup_ui.page + (key == K_RIGHT ? 1 : PAGES - 1)) % PAGES);
        eup_ui.dirty = 1;
        return take(key);
    case K_UP:
    case K_DOWN:
    case K_YES:                         /* FUNC + YES: COMMIT, later (v0.4) */
        return take(key);
    case K_PLAY:
    case K_STOP:
    case K_RECORD:
    case K_TRK:
    case K_FUNC:
        eup_ui.dirty = 1;               /* the active track may change under the page */
        return 0;
    default:
        if (key >= K_TRIG1 && key <= K_TRIG16) {
            eup_ui.dirty = 1;
            return 0;
        }
        if (key >= K_PUSH_A && key <= K_PUSH_H)
            return take(key);
        ui_open(0);                     /* anything else leaves EUC+ and does its stock job */
        return 0;
    }
}

int eup_enc(void *brain, void *ev)
{
    int32_t enc = *(const int32_t *)((const uint8_t *)ev + 12);
    int32_t d = *(const int32_t *)((const uint8_t *)ev + 16), v;
    struct eup_track *c;
    const struct par *p;
    (void)brain;

    if (!eup_ui.open || enc < 1 || enc > 8)
        return 0;                       /* the LEVEL encoder works as stock */
    p = &pages[eup_ui.page][enc - 1];
    if (p->kind == P_NONE)
        return 1;
    c = &eup_cfg.t[active_track()];
    v = par_get(c, p) + d;
    v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
    *((uint8_t *)c + p->off) = (uint8_t)v;
    eup_ui.dirty = 1;
    return 1;
}
