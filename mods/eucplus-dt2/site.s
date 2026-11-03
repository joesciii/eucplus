| SPDX-License-Identifier: GPL-2.0-or-later
| EUC+ sites on the Digitakt II OS 1.17.
| The audio fire 0x400d6982 ends `move.l %fp,84(%a3); move.l %a3,%d0` at 0x400d6e10:
| a3 = the event it built, d3 = the step it read (the source step in Euclid mode).
| Its frame (52 bytes of saved registers below the return): track 56(sp), pattern 60(sp),
| 64(sp) a per-track buffer, the playing step 68(sp). Inside this routine everything is 4 further.
        .section .run, "ax"
        .globl  eup_site_afire
eup_site_afire:
        move.l  %fp, 84(%a3)            | the displaced store
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        lea     20(%sp), %a0
        move.l  %a0, -(%sp)             | the fire's frame (its sp)
        move.l  %d3, -(%sp)             | source step
        move.l  16+8+4+68(%sp), -(%sp)  | playing step (16 saved, 8 pushed, 4 return)
        move.l  16+12+4+60(%sp), -(%sp) | pattern
        move.l  16+16+4+56(%sp), -(%sp) | track
        move.l  %a3, -(%sp)             | event
        jsr     eup_afire
        lea     24(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        move.l  %a3, %d0                | the displaced move
        rts
