/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+ v0.2 for the Digitakt II OS 1.17: T-1-style generated notes on top of the stock
 * Euclidean sequencer (docs/EXTENDED-EUCLID.md).
 *
 * Both step fires (audio and MIDI) call eup_fire with the event they built. On a track whose
 * stock Euclidean mode is on, and on a step with no parameter locks, EUC+ replaces the event's
 * note with one chosen from a pool of scale degrees in the pattern's KB SCALE and ROOT NOTE,
 * moved by a phrase shape times RANGE and by a seeded, lockable random sequence (the T-1's
 * Random). Everything else plays as stock. Interrupt level: no firmware calls, no allocation.
 *
 * The settings are eup_cfg (defaults below), edited on the EUC+ pages (ui.c). */
#include <stdint.h>
#include "eucplus.h"

/* ---- the firmware's layout (docs/RESEARCH-LOG.md) ---------------------------------------- */
#define TRACK_STRIDE    1701            /* a track's record in the pattern */
#define TR_VEL          1152
#define TR_NOTE         1157            /* NOTE, 60 = C5 (the sample's own pitch on audio tracks) */
#define TR_EUCLID       1176            /* 1: Euclidean mode on */
#define PAT_LOCK_INDEX  (2 * 38184)     /* s16 [16][128]: a step's lock list, -1 none */
#define PAT_KB_SCALE    121725          /* s8: -1 chromatic, else the scale (App. D order, from 0) */
#define PAT_KB_ROOT     121727          /* u8: 0 = C */
#define EV_NOTE         38              /* the event's note byte (written by the fire without locks) */
#define EV_VEL          39

/* ---- scales: the Digitakt II's KB SCALE list, after CHROMATIC (index -1) ----------------- */
static const uint16_t scale_mask[SCALES] = {          /* bit n: semitone n above the root */
    0x0AB5, /* Ionian (Major)        0 2 4 5 7 9 11  */
    0x06AD, /* Dorian                0 2 3 5 7 9 10  */
    0x05AB, /* Phrygian              0 1 3 5 7 8 10  */
    0x0AD5, /* Lydian                0 2 4 6 7 9 11  */
    0x06B5, /* Mixolydian            0 2 4 5 7 9 10  */
    0x05AD, /* Aeolian (Minor)       0 2 3 5 7 8 10  */
    0x056B, /* Locrian               0 1 3 5 6 8 10  */
    0x04A9, /* Pentatonic Minor      0 3 5 7 10      */
    0x0295, /* Pentatonic Major      0 2 4 7 9       */
    0x0AAD, /* Melodic Minor         0 2 3 5 7 9 11  */
    0x09AD, /* Harmonic Minor        0 2 3 5 7 8 11  */
    0x0555, /* Whole Tone            0 2 4 6 8 10    */
    0x04E9, /* Blues                 0 3 5 6 7 10    */
    0x0FAD, /* Combo Minor           0 2 3 5 7 8 9 10 11 */
    0x0973, /* Persian               0 1 4 5 6 8 11  */
    0x0463, /* Iwato                 0 1 5 6 10      */
    0x04A3, /* In-Sen                0 1 5 7 10      */
    0x018D, /* Hirajoshi             0 2 3 7 8       */
    0x018B, /* Pelog                 0 1 3 7 8       */
    0x05B3, /* Phrygian Dominant     0 1 4 5 7 8 10  */
    0x0B6D, /* Whole-Half Dim        0 2 3 5 6 8 9 11 */
    0x06DB, /* Half-Whole Dim        0 1 3 4 6 7 9 10 */
    0x057B, /* Spanish               0 1 3 4 5 6 8 10 */
    0x0575, /* Major Locrian         0 2 4 5 6 8 10  */
    0x055B, /* Super Locrian         0 1 3 4 6 8 10  */
    0x06AB, /* Dorian b2             0 1 3 5 7 9 10  */
    0x0B55, /* Lydian Augmented      0 2 4 6 8 9 11  */
    0x06D5, /* Lydian Dominant       0 2 4 6 7 9 10  */
    0x09B3, /* Double Harmonic Major 0 1 4 5 7 8 11  */
    0x0CD9, /* Lydian #2 #6          0 3 4 6 7 10 11 */
    0x039B, /* Ultraphrygian         0 1 3 4 7 8 9   */
    0x09CD, /* Hungarian Minor       0 2 3 6 7 8 11  */
    0x0673, /* Oriental              0 1 4 5 6 9 10  */
    0x0B39, /* Ionian #2 #5          0 3 4 5 8 9 11  */
    0x0367, /* Locrian bb3 bb7       0 1 2 5 6 8 9   */
    0x0FFF, /* (spare: chromatic)                    */
};

/* ---- settings (one set per track: struct eup_track, eucplus.h) ---------------------------- */

struct eup_cfg eup_cfg = {
    0x4555432b, 1, {0, 0, 0},
    {
#define DEF { 0, 0x15, 0, 4, PH_RAND, 2, 1, 1, 24, 3, SCL_FOLLOW, ROOT_FOLLOW }
        DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF, DEF
#undef DEF
    }
};

/* ---- per-track playback state ------------------------------------------------------------- */
struct eup_state {
    int16_t last_step;                  /* the playing step of the last fire */
    uint16_t pass;                      /* pattern passes since playback reached step 0 */
    uint16_t hit;                       /* hits in this pass */
    int8_t rnd[16];                     /* the random sequence */
    uint8_t seeded;                     /* seed the sequence was made from, +1 */
    volatile uint8_t restart;           /* the sequencer stopped: start from the top */
};
static struct eup_state st[TRACKS];

void eup_transport_stopped(void)
{
    int i;
    for (i = 0; i < TRACKS; i++)
        st[i].restart = 1;
}

/* ---- the debug ring the emulator tests read ------------------------------------------------ */
struct eup_rec {
    uint8_t track, step, src, note_in;
    uint8_t note_out, degree_lo, pass_lo, flags;        /* flags: 1 euclid, 2 locked step, 4 changed, 8 MIDI fire */
};
#define LOG_N 64
struct eup_rec eup_log[LOG_N];
uint32_t eup_seq;

/* ---- helpers ------------------------------------------------------------------------------ */
static uint32_t xs(uint32_t x)          /* xorshift32 */
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static void reseed(struct eup_state *s, int32_t track, uint8_t seed)
{
    uint32_t x = 0x9E3779B9u ^ ((uint32_t)seed << 8) ^ (uint32_t)(track * 0x85EBCA6Bu);
    int i;
    if (!x)
        x = 1;
    for (i = 0; i < 16; i++) {
        x = xs(x);
        s->rnd[i] = (int8_t)((int32_t)(x >> 8) % 255 - 127);    /* -127..127 */
    }
    s->seeded = (uint8_t)(seed + 1);
}

static void drift(struct eup_state *s, int32_t track, uint32_t pass, uint8_t var)
{
    uint32_t x = xs(0xA5A5A5A5u ^ (pass * 2654435761u) ^ (uint32_t)track);
    int i;
    for (i = 0; i < 16; i++) {
        x = xs(x);
        if ((x & 127) < var)
            s->rnd[i] = (int8_t)((int32_t)((x >> 9) % 255) - 127);
    }
}

/* the phrase's value at position pos of len, -64..64 */
static int32_t phrase(uint8_t shape, uint32_t pos, uint32_t len, const struct eup_state *s, uint32_t hit)
{
    static const int8_t sine16[16] = {0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24};
    static const int8_t cad[4][8] = {   /* cadence-like contours, -64..64 */
        {0, 0, 32, 0, 64, 32, 0, -32},
        {0, 32, 64, 32, 0, -32, -64, 0},
        {0, 0, 0, 32, 0, 0, 64, 0},
        {64, 32, 0, 32, -32, 0, -64, 0},
    };
    int32_t p;
    if (!len)
        len = 1;
    p = (int32_t)((pos % len) * 128 / len);             /* 0..127 */
    switch (shape) {
    case PH_SAW:   return p - 64;
    case PH_TRI:   return p < 64 ? p * 2 - 64 : 191 - p * 2;
    case PH_SINE:  return sine16[p >> 3];
    case PH_PULSE: return p < 64 ? 64 : -64;
    case PH_CAD1: case PH_CAD2: case PH_CAD3: case PH_CAD4:
        return cad[shape - PH_CAD1][p >> 4];
    default:       return s->rnd[(hit * 7 + 3) & 15] / 2;    /* PH_RAND: the sequence, offset */
    }
}

/* scale degree -> semitones from the root (negative degrees go down) */
static int32_t degree_semis(int32_t scale, int32_t deg)
{
    uint16_t mask = (scale >= 0 && scale < SCALES) ? scale_mask[scale] : 0x0FFF;
    int32_t steps[12], n = 0, i, oct;
    for (i = 0; i < 12; i++)
        if (mask & (1u << i))
            steps[n++] = i;
    if (!n)
        return deg;
    oct = deg >= 0 ? deg / n : -((-deg + n - 1) / n);
    return 12 * oct + steps[deg - oct * n];
}

static int32_t pool_degree(uint8_t pool, uint32_t k)
{
    int32_t list[8], n = 0, i;
    for (i = 0; i < 8; i++)
        if (pool & (1u << i))
            list[n++] = i;
    return n ? list[k % (uint32_t)n] : 0;
}

/* ---- the fire hook -------------------------------------------------------------------------- */
void eup_fire(uint8_t *ev, int32_t track, const uint8_t *pat, int32_t step, int32_t src,
              int32_t is_midi)
{
    const uint8_t *trk;
    const struct eup_track *c;
    struct eup_state *s;
    struct eup_rec *r;
    int32_t locked, deg, note, scale, root, lo, hi;
    uint32_t pos;
    uint8_t flags = 0, note_in;
    int stopped;

    if (track < 0 || track >= TRACKS || !pat || !ev || src < 0 || src >= 128)
        return;
    trk = pat + TRACK_STRIDE * track;
    c = &eup_cfg.t[track];
    s = &st[track];

    /* PLAY after STOP: the phrase and the random sequence start from the top (a pause, then
     * PLAY, goes on where it was: the sequencer is not stopped in between). A fire while the
     * sequencer is stopped is the first event of the next PLAY, which STOP builds ahead: it is
     * the riff's first note too, and the next fire starts from the top again. */
    stopped = EUP_SEQ_RUN == 0;
    if (stopped)
        s->restart = 1;
    if (s->restart) {
        if (!stopped)
            s->restart = 0;
        s->pass = 0;
        s->hit = 0;
        s->seeded = 0;
    } else if (step < s->last_step) {   /* the clock: a pass ends when the playing step goes back */
        s->pass++;
        s->hit = 0;
        if (!c->lock && c->var)
            drift(s, track, s->pass, c->var);
    }
    s->last_step = (int16_t)step;

    if (trk[TR_EUCLID] != 1)
        return;
    flags |= 1;
    if (s->seeded != (uint8_t)(c->seed + 1))
        reseed(s, track, c->seed);

    locked = (int16_t)((pat[PAT_LOCK_INDEX + 2 * (128 * track + src)] << 8) |
                       pat[PAT_LOCK_INDEX + 2 * (128 * track + src) + 1]) >= 0;
    if (locked)
        flags |= 2;
    note_in = ev[EV_NOTE];

    if (eup_cfg.enable && c->on && !locked) {
        /* the hit's place in the phrase: in Euclid mode the source step is the hit's index in the
         * pass, so a repeated or extra fire cannot shift the riff */
        uint32_t plen = c->plen ? c->plen : 1;
        uint32_t k = (uint32_t)(s->pass % plen) * 128u + (uint32_t)src;
        pos = (uint32_t)(s->pass % plen) * 128u + (uint32_t)step;
        deg = c->ptch + pool_degree(c->pool, k);
        deg += phrase(c->phrs, pos, plen * 128u, s, k) * c->rnge / 64;
        deg += (int32_t)s->rnd[(k * 5 + (k >> 7)) & 15] * c->ramt / 127;
        scale = c->scl == SCL_FOLLOW ? (int8_t)pat[PAT_KB_SCALE] : (int32_t)c->scl - 2;
        root = c->root == ROOT_FOLLOW ? pat[PAT_KB_ROOT] % 12 : (c->root - 1) % 12;
        note = trk[TR_NOTE] + root + degree_semis(scale, deg);
        lo = is_midi ? 0 : 36;          /* audio NOTE spans 36..84 around the sample's own 60 */
        hi = is_midi ? 127 : 84;
        while (note < lo)
            note += 12;
        while (note > hi)
            note -= 12;
        ev[EV_NOTE] = (uint8_t)note;
        flags |= 4;
        r = &eup_log[eup_seq % LOG_N];
        r->degree_lo = (uint8_t)deg;
    } else {
        r = &eup_log[eup_seq % LOG_N];
        r->degree_lo = 0;
    }
    r->track = (uint8_t)track;
    r->step = (uint8_t)step;
    r->src = (uint8_t)src;
    r->note_in = note_in;
    r->note_out = ev[EV_NOTE];
    r->pass_lo = (uint8_t)s->pass;
    r->flags = flags | (is_midi ? 8 : 0);
    eup_seq++;
    s->hit++;
}
