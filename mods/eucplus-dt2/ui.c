/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+ v0.2: the EUC+ pages (Digitakt II OS 1.17, core-dt2 1.0's ev_tick / ev_draw / ev_key / ev_enc).
 *
 * Open them from the stock SEQUENCER (Euclidean) page with a second FUNC + AMP. FUNC + AMP again,
 * or NO, goes back to it. LEFT / RIGHT change page; encoders A-H edit the active track's settings.
 * A page key (SRC, FLTR, AMP, FX, MOD) or any other menu combination closes EUC+ and does its
 * stock job. Transport, TRK, the trig keys and FUNC keep working underneath.
 *
 * ON (NOTES, encoder H) is EUC+'s own per-track switch. Opening or closing the pages never
 * changes it, and every page shows it (the switch on the left, under the page box).
 *
 * The pages are drawn in the stock parameter pages' geometry (docs/EXTENDED-EUCLID.md, "The EUC+
 * screens"): the stock header's pattern box stays and EUC+ titles the bar; the page box on the
 * left; knobs of radius 8 on the stock grid, the label under each, the value in its place and in
 * the title while it turns. All drawing goes through a canvas (struct canvas), so the same code
 * renders on the host for test/ui_host_test.c.
 *
 * These handlers run on the UI task (not at interrupt level), so they may call the firmware's
 * drawing routines and its active-track getters (docs/RESEARCH-LOG.md, "v0.2: the UI"). */
#include <stdint.h>
#include "eucplus.h"

/* ---- keys (ev + 12) and flags (ev + 16) ----------------------------------------------------- */
enum { K_SRC = 2, K_FLTR, K_AMP, K_FX, K_MOD, K_YES = 10, K_UP, K_NO, K_LEFT, K_DOWN, K_RIGHT,
       K_TRK, K_FUNC, K_KEYBOARD, K_RECORD, K_PLAY, K_STOP, K_TRIG1 = 25, K_TRIG16 = 40,
       K_PUSH_A = 41, K_PUSH_H = 48, K_MAX = 64 };
#define KF_PRESS    1
#define KF_FUNC     2
#define KF_REPEAT   8
#define KF_RELEASE  0x10

#define TOUCH_TICKS 45          /* a turned knob shows its value for 1.5 s (30 ticks a second) */

/* ---- the pages ---------------------------------------------------------------------------- */
enum { P_NUM, P_SNUM, P_LIST, P_TOGGLE, P_NONE };
struct par {
    const char *label;                  /* under the knob, as the stock labels */
    const char *name;                   /* in the title while it turns */
    uint8_t off;                        /* in struct eup_track */
    uint8_t kind;
    int16_t lo, hi;
    const char *const *names;           /* P_LIST: the value under the knob */
    const char *const *long_names;      /* P_LIST: the value in the title (0: names) */
};

#define OFF(f)  ((uint8_t)__builtin_offsetof(struct eup_track, f))
static const char *const n_off[2] = { "OFF", "ON" };
static const char *const n_phrs[PH_COUNT] = { "RAND", "SAW", "TRI", "SINE", "PULS", "CAD1", "CAD2",
                                              "CAD3", "CAD4" };
static const char *const n_phrs_long[PH_COUNT] = { "Random", "Saw", "Triangle", "Sine", "Pulse",
                                                   "Cadence 1", "Cadence 2", "Cadence 3", "Cadence 4" };
static const char *const n_scl[SCL_COUNT] = {
    "FLW", "CHRM", "IONI", "DORI", "PHRY", "LYDI", "MIXO", "AEOL", "LOCR", "PMIN", "PMAJ",
    "MMIN", "HMIN", "WHOL", "BLUE", "CMIN", "PERS", "IWAT", "INSN", "HIRA", "PELO", "PDOM",
    "WHDM", "HWDM", "SPAN", "MLOC", "SLOC", "DOB2", "LAUG", "LDOM", "DHMJ", "L#26", "UPHR",
    "HUNM", "ORNT", "I#25", "LB37",
};
static const char *const n_scl_long[SCL_COUNT] = {
    "Follow KB", "Chromatic", "Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian",
    "Locrian", "Pent Minor", "Pent Major", "Melodic Min", "Harmonic Min", "Whole Tone", "Blues",
    "Combo Minor", "Persian", "Iwato", "In-Sen", "Hirajoshi", "Pelog", "Phryg Dom", "Whole-Half",
    "Half-Whole", "Spanish", "Major Locr", "Super Locr", "Dorian b2", "Lydian Aug", "Lydian Dom",
    "Dbl Harm Maj", "Lydian #2#6", "Ultraphryg", "Hungar Min", "Oriental", "Ionian #2#5",
    "Locr bb3bb7",
};
static const char *const n_root[ROOT_COUNT] = { "FLW", "C", "C#", "D", "D#", "E", "F", "F#", "G",
                                                "G#", "A", "A#", "B" };
static const char *const n_root_long[ROOT_COUNT] = { "Follow KB", "C", "C#", "D", "D#", "E", "F",
                                                     "F#", "G", "G#", "A", "A#", "B" };

#define PAGES 2
static const char *const page_name[PAGES] = { "Notes", "Random" };
static const struct par pages[PAGES][8] = {
    {
        { "PTCH", "Pitch", OFF(ptch), P_SNUM, -24, 24, 0, 0 },
        { "RNGE", "Range", OFF(rnge), P_NUM, 0, 24, 0, 0 },
        { "PHRS", "Phrase", OFF(phrs), P_LIST, 0, PH_COUNT - 1, n_phrs, n_phrs_long },
        { "PLEN", "Phrase Len", OFF(plen), P_NUM, 1, 64, 0, 0 },
        { 0, 0, 0, P_NONE, 0, 0, 0, 0 },                    /* HARM (v0.4) */
        { "SCL", "Scale", OFF(scl), P_LIST, 0, SCL_COUNT - 1, n_scl, n_scl_long },
        { "ROOT", "Root", OFF(root), P_LIST, 0, ROOT_COUNT - 1, n_root, n_root_long },
        { "ON", "EUC+", OFF(on), P_TOGGLE, 0, 1, n_off, 0 },
    },
    {
        { "VAR", "Variance", OFF(var), P_NUM, 0, 127, 0, 0 },
        { "SEED", "Seed", OFF(seed), P_NUM, 0, 255, 0, 0 },
        { "LOCK", "Lock", OFF(lock), P_TOGGLE, 0, 1, n_off, 0 },
        { "RAMT", "Rnd Amount", OFF(ramt), P_NUM, 0, 24, 0, 0 },
        { 0, 0, 0, P_NONE, 0, 0, 0, 0 },
        { 0, 0, 0, P_NONE, 0, 0, 0, 0 },
        { 0, 0, 0, P_NONE, 0, 0, 0, 0 },
        { 0, 0, 0, P_NONE, 0, 0, 0, 0 },
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
    uint32_t ticks;             /* ev_tick count */
    uint32_t touch_at;          /* the tick the touched knob last turned */
    int8_t touch;               /* the touched knob (0-7), -1 none */
    uint8_t taken[K_MAX];       /* keys whose press EUC+ took: it takes their release too */
};
struct eup_ui eup_ui = { .touch = -1 };

static int32_t active_track(void)
{
    int32_t t;
#ifdef EUP_HOST
    t = EUP_ACTIVE_TRACK();
#else
    uint32_t ctx = EUP_SEQ_CTX();
    t = ctx ? EUP_SEQ_ACTIVE(EUP_SEQ_TRACKSEL(ctx)) : 0;
#endif
    return t < 0 ? 0 : t >= TRACKS ? TRACKS - 1 : t;
}

static int touched(int k)
{
    return eup_ui.touch == k && eup_ui.ticks - eup_ui.touch_at < TOUCH_TICKS;
}

/* ---- drawing: the canvas (rows from the top, 0-63; columns 0-127) --------------------------- */
enum { F_LABEL, F_NARROW, F_TITLE };
#define T_CENTRE    2
#define T_RIGHT     4
struct canvas {
    void *ctx;
    void (*fill)(void *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
    void (*text)(void *ctx, int32_t font, int32_t x, int32_t ytop, int32_t flags, const char *s);
};                                      /* F_TITLE: from x only (no T_ flags) */

static void fill(const struct canvas *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t col)
{
    c->fill(c->ctx, x0, y0, x1, y1, col);
}

static void text(const struct canvas *c, int32_t font, int32_t x, int32_t ytop, int32_t flags,
                 const char *s)
{
    c->text(c->ctx, font, x, ytop, flags, s);
}

static void px(const struct canvas *c, int32_t x, int32_t y)
{
    if (x >= 0 && x < 128 && y >= 0 && y < 64)
        fill(c, x, y, x, y, 1);
}

static void hline(const struct canvas *c, int32_t x0, int32_t x1, int32_t y) { fill(c, x0, y, x1, y, 1); }
static void vline(const struct canvas *c, int32_t x, int32_t y0, int32_t y1) { fill(c, x, y0, x, y1, 1); }

static void line(const struct canvas *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, e2;
    for (;;) {
        px(c, x0, y0);
        if (x0 == x1 && y0 == y1)
            return;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void circle(const struct canvas *c, int32_t cx, int32_t cy, int32_t r)
{
    int32_t x = r, y = 0, err = 1 - r;
    while (x >= y) {
        px(c, cx + x, cy + y); px(c, cx - x, cy + y); px(c, cx + x, cy - y); px(c, cx - x, cy - y);
        px(c, cx + y, cy + x); px(c, cx - y, cy + x); px(c, cx + y, cy - x); px(c, cx - y, cy - x);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static const uint16_t sin_q10[91] = {        /* sin(a degrees) x 1024 */
    0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213, 230, 248, 265, 282, 299, 316, 333,
    350, 367, 384, 400, 416, 433, 449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630,
    644, 658, 672, 685, 698, 711, 724, 737, 749, 761, 773, 784, 796, 807, 818, 828, 839, 849, 859,
    868, 878, 887, 896, 904, 912, 920, 928, 935, 943, 949, 956, 962, 968, 974, 979, 984, 989, 994,
    998, 1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024, 1024,
};

static int32_t isin(int32_t a)              /* -180..180 degrees */
{
    int32_t neg = a < 0;
    if (neg)
        a = -a;
    if (a > 90)
        a = 180 - a;
    return neg ? -(int32_t)sin_q10[a] : (int32_t)sin_q10[a];
}

static int32_t q10(int32_t n) { return n >= 0 ? (n + 512) >> 10 : -((-n + 512) >> 10); }

/* the stock knob: a circle of radius 8, a pointer from the centre, the travel +-135 degrees
 * (up is the middle) */
#define KNOB_R 8
static void knob(const struct canvas *c, int32_t cx, int32_t cy, int32_t a)
{
    int32_t len = KNOB_R - 1;
    int32_t cosa = isin(a >= 90 ? a - 270 : a + 90);
    circle(c, cx, cy, KNOB_R);
    line(c, cx, cy, cx + q10(isin(a) * len), cy - q10(cosa * len));
}

/* a toggle, as the stock EUC switch: a rounded box 17 x 9; on, filled with the knob dark on the
 * right; off, an outline with the knob on the left */
static void toggle(const struct canvas *c, int32_t cx, int32_t cy, int32_t on)
{
    int32_t x0 = cx - 8, x1 = cx + 8, y0 = cy - 4, y1 = cy + 4;
    hline(c, x0 + 1, x1 - 1, y0);
    hline(c, x0 + 1, x1 - 1, y1);
    vline(c, x0, y0 + 1, y1 - 1);
    vline(c, x1, y0 + 1, y1 - 1);
    if (on) {
        fill(c, x0 + 1, y0 + 1, x1 - 1, y1 - 1, 1);
        fill(c, x1 - 6, y0 + 2, x1 - 2, y1 - 2, 0);
    } else {
        fill(c, x0 + 2, y0 + 2, x0 + 6, y1 - 2, 1);
    }
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

static char *cat(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}

static int32_t par_get(const struct eup_track *t, const struct par *p)
{
    const uint8_t *b = (const uint8_t *)t + p->off;
    int32_t v = p->kind == P_SNUM ? (int32_t)(int8_t)*b : (int32_t)*b;
    return v < p->lo ? p->lo : v > p->hi ? p->hi : v;
}

static const char *par_text(const struct par *p, int32_t v, char *buf, int long_form)
{
    if (p->kind == P_LIST || p->kind == P_TOGGLE)
        return long_form && p->long_names ? p->long_names[v] : p->names[v];
    return num(buf, v, p->kind == P_SNUM);
}

/* the stock geometry (docs/EXTENDED-EUCLID.md): header rows 0-9, the page box x 0-14 rows 13-36,
 * the knob grid's centres x 33, 59, 85, 111 and rows 21 and 48, labels at rows 32 and 59 */
#define COL_X(i)    (33 + 26 * (i))
#define ROW_Y(r)    ((r) ? 48 : 21)
#define LABEL_Y(r)  ((r) ? 59 : 32)

static void draw_header(const struct canvas *c, int32_t track)
{
    char buf[40], *e;
    const struct par *p = 0;
    if (eup_ui.touch >= 0 && touched(eup_ui.touch))
        p = &pages[eup_ui.page][eup_ui.touch];
    /* the title bar (the stock pattern box at x 0-14 stays): its text, then inverted */
    fill(c, 16, 0, 127, 9, 0);
    if (p && p->kind != P_NONE) {            /* a turning knob: its name and value, as stock */
        char v[12];
        e = cat(buf, p->name);
        e = cat(e, ": ");
        cat(e, par_text(p, par_get(&eup_cfg.t[track], p), v, 1));
    } else {
        e = cat(buf, "EUC+ ");
        e = cat(e, page_name[eup_ui.page]);
        e = cat(e, " (");
        e = cat(e, num(e, eup_ui.page + 1, 0));
        e = cat(e, "/");
        e = cat(e, num(e, PAGES, 0));
        cat(e, ")");
    }
    text(c, F_TITLE, 20, 2, 0, buf);
    buf[0] = 'T';
    num(buf + 1, track + 1, 0);
    text(c, F_LABEL, 125, 3, T_RIGHT, buf);
    fill(c, 18, 0, 126, 9, -1);
    fill(c, 17, 1, 17, 8, -1);
    fill(c, 127, 1, 127, 8, -1);
}

/* the page box, as the stock track box (corners cut), then EUC+'s ON switch where the stock
 * pages have the LEV fader: on every page, so it is always plain whether EUC+ drives the track */
static void draw_left(const struct canvas *c, int32_t track)
{
    char buf[4];
    int32_t on = eup_cfg.t[track].on;
    hline(c, 1, 13, 13);
    hline(c, 1, 13, 36);
    vline(c, 0, 14, 35);
    vline(c, 14, 14, 35);
    text(c, F_NARROW, 7, 16, T_CENTRE, "EUC");
    text(c, F_LABEL, 7, 25, T_CENTRE, num(buf, track + 1, 0));
    /* the switch: a box rows 40-56, filled when on */
    hline(c, 3, 11, 40);
    hline(c, 3, 11, 56);
    vline(c, 2, 41, 55);
    vline(c, 12, 41, 55);
    if (on)
        fill(c, 4, 42, 10, 54, 1);
    else
        fill(c, 7, 47, 7, 49, 1);
    text(c, F_LABEL, 7, 59, T_CENTRE, on ? "ON" : "OFF");
}

static void draw_slot(const struct canvas *c, const struct eup_track *t, int32_t i)
{
    const struct par *p = &pages[eup_ui.page][i];
    int32_t col = i & 3, row = i >> 2, cx = COL_X(col), cy = ROW_Y(row), v;
    char buf[12];
    if (p->kind == P_NONE)
        return;
    v = par_get(t, p);
    if (p->kind == P_NUM || p->kind == P_SNUM)
        knob(c, cx, cy, -135 + 270 * (v - p->lo) / (p->hi - p->lo));
    else if (p->kind == P_TOGGLE)
        toggle(c, cx, cy, v);
    else                                    /* a list: its value, where the knob would be */
        text(c, F_LABEL, cx, cy - 2, T_CENTRE, p->names[v]);
    if (touched(i))                         /* turning: the value in the label's place */
        text(c, F_LABEL, cx, LABEL_Y(row), T_CENTRE | 0x18, par_text(p, v, buf, 0));
    else
        text(c, F_LABEL, cx, LABEL_Y(row), T_CENTRE, p->label);
}

static void render(const struct canvas *c, int32_t track)
{
    const struct eup_track *t = &eup_cfg.t[track];
    int32_t i;
    fill(c, 0, 10, 127, 63, 0);
    draw_header(c, track);
    draw_left(c, track);
    for (i = 0; i < 8; i++)
        draw_slot(c, t, i);
}

#ifdef EUP_HOST
/* test/ui_host_test.c draws the pages on its own canvas */
void eup_ui_render(void *ctx, void (*f)(void *, int32_t, int32_t, int32_t, int32_t, int32_t),
                   void (*tx)(void *, int32_t, int32_t, int32_t, int32_t, const char *))
{
    struct canvas c = { ctx, f, tx };
    render(&c, active_track());
}
void eup_host_set_euclid(int on) { eup_ui.euclid = (uint8_t)on; }
int eup_host_open(void) { return eup_ui.open; }
int eup_host_page(void) { return eup_ui.page; }
#else
/* ---- the firmware canvas ------------------------------------------------------------------ */
static void fw_fill(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{
    EUP_FILLRECT(bmp, x0, 63 - y1, x1, 63 - y0, colour);
}

static void fw_text(void *bmp, int32_t font, int32_t x, int32_t ytop, int32_t flags, const char *s)
{
    if (font == F_TITLE)                /* a font record, not a font object: the plain drawer, from x */
        EUP_DRAWSTR(bmp, EUP_FONT_TITLE, x, 63 - (ytop + 5), 32, s);
    else
        EUP_TEXTF(bmp, font == F_NARROW ? EUP_FONT_NARROW : EUP_FONT_LABEL, x, 63 - (ytop + 4),
                  flags, "%s", s);
}

/* A Bitmap: +4 width, +8 height, +12 longs per column, +16 the pixels: column x, row y (0 = the
 * bottom row) is bit 31 - y % 32 of long x * stride + y / 32. */
#define BMP_W(b)        (((uint32_t *)(b))[1])
#define BMP_H(b)        (((uint32_t *)(b))[2])
#define BMP_STRIDE(b)   (((uint32_t *)(b))[3])
#define BMP_DATA(b)     (((uint32_t **)(b))[4])

/* The stock SEQUENCER page: a signature of its two rows of parameter labels (PL1 PL2 LEN EUC,
 * RO1 RO2 TRO OP: rows 27-31 and 0-4 from the bottom, columns 18-127). They are the same on audio
 * and MIDI tracks and with EUC on or off; the title bar is not used, since it shows the last
 * parameter edited ("Pulses Gen 1: 5"). Measured in the emulator (docs/RESEARCH-LOG.md). */
static uint32_t page_sig(void *bmp)
{
    const uint32_t *d = BMP_DATA(bmp);
    uint32_t st = BMP_STRIDE(bmp), h = 5381, w;
    int32_t x;
    if (!d || BMP_W(bmp) != 128 || BMP_H(bmp) != 64 || st != 2)
        return 0;
    for (x = 18; x <= 127; x++) {
        w = d[x * st];                                      /* rows 0-31 */
        h = (h * 33) ^ ((w >> 27) | (w & 0x1f) << 5);       /* rows 0-4 and 27-31 */
    }
    return h;
}

void eup_draw(void *bmp, void *ctrl)
{
    struct canvas c = { bmp, fw_fill, fw_text };
    (void)ctrl;
    eup_ui.sig = page_sig(bmp);
    eup_ui.euclid = eup_ui.sig == EUP_EUCLID_PAGE_SIG;
    if (eup_ui.open)
        render(&c, active_track());
}
#endif

/* ---- the events --------------------------------------------------------------------------- */
void eup_tick(void *ctrl)
{
    eup_ui.ticks++;
    if (EUP_SEQ_RUN == 0)
        eup_transport_stopped();
    if (eup_ui.open && eup_ui.touch >= 0 && !touched(eup_ui.touch)) {
        eup_ui.touch = -1;              /* the value's time is up: the label and title come back */
        eup_ui.dirty = 1;
    }
    if (eup_ui.dirty) {
        eup_ui.dirty = 0;
        *((volatile uint8_t *)ctrl + 0x20) = 1;
    }
}

static void ui_open(int open)
{
    eup_ui.open = (uint8_t)open;
    eup_ui.touch = -1;
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
        eup_ui.touch = -1;
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
    struct eup_track *t;
    const struct par *p;
    (void)brain;

    if (!eup_ui.open || enc < 1 || enc > 8)
        return 0;                       /* the LEVEL encoder works as stock */
    p = &pages[eup_ui.page][enc - 1];
    if (p->kind == P_NONE)
        return 1;
    t = &eup_cfg.t[active_track()];
    v = par_get(t, p) + d;
    v = v < p->lo ? p->lo : v > p->hi ? p->hi : v;
    *((uint8_t *)t + p->off) = (uint8_t)v;
    eup_ui.touch = (int8_t)(enc - 1);
    eup_ui.touch_at = eup_ui.ticks;
    eup_ui.dirty = 1;
    return 1;
}
