/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+ (Digitakt II OS 1.17), step 0: a probe. Each audio-track fire's event is logged into
 * eup_log, a ring the emulator reads, to pin down the event's fields and the pattern's address
 * before EUC+ changes anything. Interrupt level: no firmware calls. */
#include <stdint.h>

#define TRACK_STRIDE 1701
#define TR_EUCLID    1176               /* 1: Euclidean mode on */

struct eup_rec {
    uint32_t magic_seq;                 /* 0xE0C00000 | sequence number */
    int32_t track, step, src;
    const uint8_t *pat;
    int32_t ev[8];                      /* event words +56 .. +84 */
    int32_t arg[8];                     /* the fire's frame +56 .. +84 (its arguments) */
    uint8_t euclid, lock384, lock256, lock512;
};

#define LOG_N 32
struct eup_rec eup_log[LOG_N];
uint32_t eup_seq;

void eup_afire(int32_t *ev, int32_t track, const uint8_t *pat, int32_t step, int32_t src,
               const int32_t *frame)
{
    struct eup_rec *r = &eup_log[eup_seq % LOG_N];
    const uint8_t *trk = pat + TRACK_STRIDE * track;
    int i;
    r->track = track;
    r->step = step;
    r->src = src;
    r->pat = pat;
    for (i = 0; i < 8; i++)
        r->ev[i] = ev[14 + i];
    for (i = 0; i < 8; i++)
        r->arg[i] = frame[14 + i];
    r->euclid = trk[TR_EUCLID];
    r->lock384 = (src >= 0 && src < 128) ? trk[384 + src] : 0xee;
    r->lock256 = (src >= 0 && src < 128) ? trk[256 + src] : 0xee;
    r->lock512 = (src >= 0 && src < 128) ? trk[512 + src] : 0xee;
    r->magic_seq = 0xE0C00000u | (eup_seq & 0xffff);
    eup_seq++;
}
