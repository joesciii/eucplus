/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+: what the engine (eucplus.c) and the UI (ui.c) share. */
#ifndef EUCPLUS_H
#define EUCPLUS_H
#include <stdint.h>

#define TRACKS  16
#define SCALES  36      /* the KB SCALE list after CHROMATIC (35) and a spare chromatic */

enum { PH_RAND, PH_SAW, PH_TRI, PH_SINE, PH_PULSE, PH_CAD1, PH_CAD2, PH_CAD3, PH_CAD4, PH_COUNT };

/* SCL: 0 FOLLOW (the pattern's KB SCALE), 1 CHROMATIC, 2 + n the KB SCALE list's scale n.
 * ROOT: 0 FOLLOW (the pattern's ROOT NOTE), 1 + n the note n (0 = C). */
#define SCL_FOLLOW      0
#define SCL_CHROMATIC   1
#define SCL_COUNT       (2 + 35)
#define ROOT_FOLLOW     0
#define ROOT_COUNT      13

struct eup_track {
    uint8_t on;         /* EUC+ notes on this track (only while stock Euclid is on). Off by default:
                         * stock Euclid keeps its own notes until the user turns EUC+ on (the ON
                         * control on the EUC+ page; opening the page never changes it) */
    uint8_t pool;       /* bit d: scale degree d (0..7) is in the pool; 0x15 = 1-3-5 */
    int8_t  ptch;       /* transpose within the scale, in degrees (-24..24) */
    uint8_t rnge;       /* phrase movement, degrees (0..24) */
    uint8_t phrs;       /* phrase shape (PH_*) */
    uint8_t plen;       /* phrase length in pattern passes (1..64) */
    uint8_t seed;       /* the random sequence's seed */
    uint8_t lock;       /* 1: the random sequence never changes */
    uint8_t var;        /* unlocked: the chance (0..127) a value re-rolls each pass */
    uint8_t ramt;       /* random amount on pitch, degrees (0..24) */
    uint8_t scl;        /* SCL_* */
    uint8_t root;       /* ROOT_* */
};

struct eup_cfg {
    uint32_t magic;                     /* 'EUC+' */
    uint8_t  enable;                    /* the global switch */
    uint8_t  pad[3];
    struct eup_track t[TRACKS];
};

extern struct eup_cfg eup_cfg;

/* The sequencer's run state (OS 1.17): 0 stopped, 1 playing, 2 paused (docs/RESEARCH-LOG.md) */
#ifdef EUP_HOST
extern volatile int32_t eup_host_seq_run;      /* test/host_test.c */
#define EUP_SEQ_RUN     eup_host_seq_run
#else
#define EUP_SEQ_RUN     (*(volatile int32_t *)0x43164f94)
#endif

/* The UI task tells the engine that the sequencer is stopped: each track's phrase and random
 * sequence start again from the top at its next fire. */
void eup_transport_stopped(void);

#endif
