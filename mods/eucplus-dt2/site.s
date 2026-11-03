| SPDX-License-Identifier: GPL-2.0-or-later
| EUC+ sites on the Digitakt II OS 1.17 (docs/RESEARCH-LOG.md, "The Digitakt II step fire").
|
| Both fires keep 52 bytes of saved registers at their sp; their arguments follow the return
| address: track 56(sp), pattern 60(sp), a second record 64(sp), the playing step 68(sp).
| Inside a routine below, the fire's frame is 4 further away (our return address).

        .section .run, "ax"

| ---- the audio fire 0x400d6982 ---------------------------------------------------------
| 0x400d6e10, 6 bytes over `move.l %fp,84(%a3); move.l %a3,%d0`, the success path's end:
| a3 = the event, d3 = the step it read (the source step in Euclid mode).
        .globl  eup_site_afire
eup_site_afire:
        move.l  %fp, 84(%a3)            | displaced
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        clr.l   -(%sp)                  | 0: the audio fire
        move.l  %d3, -(%sp)             | source step
        move.l  16+8+4+68(%sp), -(%sp)  | playing step
        move.l  16+12+4+60(%sp), -(%sp) | pattern
        move.l  16+16+4+56(%sp), -(%sp) | track
        move.l  %a3, -(%sp)             | event
        jsr     eup_fire
        lea     24(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        move.l  %a3, %d0                | displaced
        rts

| ---- the MIDI fire 0x400d6e88 ----------------------------------------------------------
| 0x400d7234, 6 bytes over `move.l %d2,16(%fp); move.l %a3,%d0`: fp = the event,
| d4 = the step it read (the source step in Euclid mode).
        .globl  eup_site_mfire
eup_site_mfire:
        move.l  %d2, 16(%fp)            | displaced
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        pea     1.w                     | 1: the MIDI fire
        move.l  %d4, -(%sp)             | source step
        move.l  16+8+4+68(%sp), -(%sp)  | playing step
        move.l  16+12+4+60(%sp), -(%sp) | pattern
        move.l  16+16+4+56(%sp), -(%sp) | track
        move.l  %fp, -(%sp)             | event
        jsr     eup_fire
        lea     24(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        move.l  %a3, %d0                | displaced
        rts
