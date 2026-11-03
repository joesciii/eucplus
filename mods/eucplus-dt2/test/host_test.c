/* Host test: run eup_fire over a fake pattern and print the notes EUC+ chooses.
 * cc -O1 -o host_test host_test.c ../eucplus.c && ./host_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct eup_rec { uint8_t track, step, src, note_in, note_out, degree_lo, pass_lo, flags; };
extern struct eup_rec eup_log[64];
extern uint32_t eup_seq;
extern unsigned char eup_cfg[];
void eup_fire(uint8_t *ev, int32_t track, const uint8_t *pat, int32_t step, int32_t src, int32_t is_midi);

static uint8_t pat[121800];
static const char *names[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static void run(const char *title, int scale, int root, int passes)
{
    int p, s, hits[16] = {0}, n = 0, k;
    uint8_t ev[96];
    memset(pat, 0, sizeof pat);
    for (k = 0; k < 16 * 128; k++) { pat[2 * 38184 + 2 * k] = 0xff; pat[2 * 38184 + 2 * k + 1] = 0xff; }
    pat[1157] = 60; pat[1176] = 1;                     /* track 0: NOTE C5, Euclid on */
    eup_cfg[8] = 1;                                    /* EUC+ on for track 0 (off by default) */
    pat[121725] = (uint8_t)scale; pat[121727] = (uint8_t)root;
    for (s = 0; s < 16; s++) if ((s * 5) % 16 < 5) hits[n++] = s;   /* 5 of 16 */
    printf("%s (scale %d, root %s):\n", title, scale, names[root]);
    for (p = 0; p < passes; p++) {
        printf("  pass %d:", p);
        for (k = 0; k < n; k++) {
            memset(ev, 0, sizeof ev); ev[38] = 60;
            eup_fire(ev, 0, pat, hits[k], k, 0);
            printf(" %s%d", names[ev[38] % 12], ev[38] / 12 - 1);
        }
        printf("\n");
    }
}

int main(void)
{
    run("Aeolian A, locked random", 5, 9, 4);
    run("Pentatonic minor C", 7, 0, 3);
    run("Chromatic (no scale)", -1, 0, 2);
    return 0;
}
