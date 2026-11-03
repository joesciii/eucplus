/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUC+: every Digitakt II OS 1.17 address the C code uses, in one place (docs/RESEARCH-LOG.md
 * has how each was found). A port to another OS replaces this file, and site.s / mod.json's
 * sites. Built for the host (-DEUP_HOST), the firmware is replaced by the test's stubs. */
#ifndef EUCPLUS_OS117_H
#define EUCPLUS_OS117_H
#include <stdint.h>

#ifdef EUP_HOST
/* the host tests' stand-ins (test/host_test.c, test/ui_host_test.c) */
extern volatile int32_t eup_host_seq_run;
extern int32_t eup_host_active_track;
#define EUP_SEQ_RUN             eup_host_seq_run
#define EUP_ACTIVE_TRACK()      eup_host_active_track
#else

/* The sequencer's run state: 0 stopped, 1 playing, 2 paused. A plain read: safe at interrupt
 * level. */
#define EUP_SEQ_RUN             (*(volatile int32_t *)0x43164f94)

/* The UI task only (they are firmware calls). These return pointers in d0, so they are
 * declared as returning integers: GCC (m68k-linux) reads a pointer result from a0. */
typedef uint32_t (*eup_get0_t)(void);
typedef uint32_t (*eup_getp_t)(uint32_t);
typedef int32_t (*eup_geti_t)(uint32_t);
#define EUP_SEQ_CTX             ((eup_get0_t)0x40198b7e)    /* the sequencer context (made at boot) */
#define EUP_SEQ_TRACKSEL        ((eup_getp_t)0x40041d18)    /* its track selection: context + 48 */
#define EUP_SEQ_ACTIVE          ((eup_geti_t)0x4004abb8)    /* the selection's active track, 0-15 */

/* Drawing (UI task). y counts from the bottom row (0) of the 128 x 64 frame. */
typedef void (*eup_fillrect_t)(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                               int32_t colour);           /* 0 clears, 1 sets, -1 inverts */
typedef int32_t (*eup_textf_t)(void *bmp, const void *font, int32_t x, int32_t y, int32_t flags,
                               const char *fmt, ...);     /* y: the text's bottom row; returns
                                                           * the width drawn */
typedef int32_t (*eup_drawstr_t)(void *bmp, const void *font, int32_t x, int32_t y, int32_t maxlen,
                                 const char *s);          /* plain text, from x; returns its width */
#define EUP_FILLRECT            ((eup_fillrect_t)0x401131ae)
#define EUP_DRAWSTR             ((eup_drawstr_t)0x40113d60)
#define EUP_TEXTF               ((eup_textf_t)0x40113dc8)
#define EUP_TEXT_CENTRE         2       /* x is the text's centre */
#define EUP_TEXT_RIGHT          4       /* x is its right edge */
#define EUP_FONT_LABEL          ((const void *)0x44d2e70c)  /* the stock labels: 5-pixel capitals */
#define EUP_FONT_NARROW         ((const void *)0x44d2e6f4)  /* the stock page box, lists: 3 pixels */
#define EUP_FONT_TITLE          ((const void *)0x402a9be0)  /* the menus' and header's 6-pixel upper
                                                             * and lower case: a font record, for
                                                             * EUP_DRAWSTR (EUP_TEXTF draws nothing
                                                             * with it) */
#endif

/* The stock SEQUENCER (Euclidean) page: the signature of its two label rows (ui.c, page_sig) */
#define EUP_EUCLID_PAGE_SIG     0xf6f7e0f0u

#endif
