# Research log

Facts gathered while working, with how each was obtained, so that work can resume in a fresh
session. **[V]** = verified here. **[D]** = deduced from disassembly, not yet run.

## Firmware files (never committed)

| File | sha256 | Notes |
|---|---|---|
| `Digitakt_II_OS1.17.syx` (from the user's upload; also at elektron.se `wp-content/uploads/2026/10/Digitakt_II_OS1.17_dist.zip`) | `26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5` | Matches Elekloader `devices/digitakt_mk2.py` **[V]** |
| DT2 main OS (section 3, depacked) | `a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964` | 3,275,616 bytes, loads at `0x40000400`, device byte `0x14` **[V]** |
| `Digitakt_OS1.54.syx` (mk1, elektron.se `…/2026/10/Digitakt_OS1.54_dist.zip`) | `f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6` | Matches Elekloader `digitakt_mk1.py` **[V]** |
| mk1 1.54 main OS | `5c58bf9e3949ef09977c5fc007a61e8d026931f67f1621238379dfb8ee4d31a2` | 2,479,680 bytes **[V]** |
| DT2 manual OS 1.17 | — | elektron.se `…/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf` |

## Reproducing the analysis

```sh
git clone --depth 1 https://github.com/irpina/elekloader
python3 -I tools/unpack.py elekloader Digitakt_II_OS1.17.syx main_1.17.bin    # depack section 3
m68k-linux-gnu-objdump -D -b binary -m m68k:cfv4e --adjust-vma=0x40000400 main_1.17.bin > main.dis
python3 -I tools/maskmatch.py mk1.dis dt2.dis 4006f4be 4006fa40 6        # where an mk1 function sits in the DT2
python3 -I tools/sitemap.py mk1.dis dt2.dis 400d6000 400d7800 <mk1 sites> # per-site alignment
```

Toolchain: Ubuntu `gcc-m68k-linux-gnu` / `binutils-m68k-linux-gnu` 13.3 (apt). For Elekloader set
`ELEKLOADER_CROSS=m68k-linux-gnu-`.

## Environment notes (cloud session)

- The WebFetch tool cannot resolve some hosts even with full network access. `curl` through
  the proxy works.
- The automatic permission check blocks running downloaded code (`uv sync` in digikit) unless a
  permission rule allows it. The rules are in `.claude/settings.json` (uv, cargo build,
  `python3 -I`, `python3 -m elekloader`). Edits to that file by Claude are refused, so the user
  adds new rules.
- `tools/setup_env.sh` rebuilds the whole environment in a fresh session, and `tools/run_e2e.sh`
  runs the test.

## The Digitakt II step fire (OS 1.17) [D]

Found by masked-opcode matching of the mk1's fire (`0x4006f4be`, the seq-events-dt1 sites) against
the DT2. 76 of 446 six-instruction shingles fall in `0x400d6800`–`0x400d6c00`.

| | mk1 1.54 | DT2 1.17 |
|---|---|---|
| audio fire, entry | `0x4006f4be` | **`0x400d6982`** (`lea -52(sp)`, `movem d2-d7/a2-a6`) |
| MIDI fire, entry | `0x4006f846` | **`0x400d6e88`** (same shape) |
| callers | — | `jsr` at `0x400d73b4` / `0x400d73fc` and `0x400d8200` / `0x400d821a` (the per-track loop at `0x400d81b0`, below) |
| track record stride | 911 (`d3 = 911·track`) | **1701** (`mulsl #1701`) |
| args after prologue | step `76(sp)`, event `80(sp)` | track `56(sp)`, pattern `60(sp)`, record `64(sp)`, playing step `68(sp)`, event `72(sp)` (0: allocate), `76(sp)`, out `80(sp)` |
| step word | `pattern + 911·t + 2·step` | `pattern + 1701·t + 2·step` (`a5 = 1701·t`) |
| trig bit / skip bit | `0x0001` / `0x0004` | the same: `0x0001` / `0x0004` |
| SYN default bit | `0x0080`, override `0x0800` | the same: `bset #7` when `+1154 & 0x80` and step bit 11 is clear |
| per-step cond bytes | — | `track + step + 640 / 768 / 896` (`896 == -1` means none) |
| condition routine | — | `jsr 0x401385e4` (6 args: track, step, c640, c768, c896, has-event) |
| per-track PROB | `908(a0)` | **`1168`** → `jsr 0x4013859c` |
| per-track byte `+1444` | — | if nonzero: `step = step mod it`, then `step = word at +2·(722+step)`. Probably a scale or length map; to check |
| **Euclid flag** | — | **`+1176`**: `seq d6` when it is 1 |
| **Euclid step map** | — | when the flag is on: `src = word at 1701·t + 2·(594+step)`. If negative, there is no hit (the event is cleared). Otherwise the fire reads step word `src`, ORs in the trig bit, keeps bit `0x10` from the grid word and sets bit 16 |
| global bytes | — | `121721` / `121722` in the pattern: a mode select used with the table at `0x4020e28c` |

What this means:
- **Stock Euclid hits pass through the same fire function as grid trigs.** The generator's result
  is a per-track map (`+1188`, 2 bytes per step) from play step to source step. EUC+ can hook here,
  as `seq-events` does on the mk1, and can tell a Euclid hit from a grid trig (the flag and the map).
- `Track::recalculateEuclidean(bool)` (lambda name strings at `0x401f5d44`) should be what writes
  the map at `+1188`. Find it next, to read PL1/PL2/RO/TRO/OP and LEN from the track record.
- The mk1 sites do not map one to one: the DT2's trig test is `tst.w` on masked words, not the
  mk1's `move.w … and.l`. The `seq-events-dt2` site list has to be made from this code, not copied.


## Emulator setup (digikit, verified 2026-11-03)

```sh
git clone --depth 1 https://github.com/m-dwyer/digikit && cd digikit
cp Digitakt_II_OS1.17.syx firmware/
uv sync
apt-get install libasound2-dev pkg-config            # the daemon's audio crate needs ALSA headers
uv run python -m tools.emulator.native_emu --daemon --cf-only --build-only firmware/Digitakt_II_OS1.17.syx
#   -> out/native/target/nogen/release/digi-emud (the GUI build needs astro and is not needed)
# DSP / paced modes need the ADSP-2156x HWR text (register reset tables):
curl -o docs/refs/adsp-2156x-hwr.pdf https://docs.ampnuts.ru/analog.com.datasheet/adsp-21566/related_data/adsp-2156x_hwr.pdf
#   11,954,389 bytes, sha256 e56b2e4e…30ec (the size digikit records; analog.com refuses scripts)
uv run --with pymupdf python -m tools.devtools.refstext docs/refs/adsp-2156x-hwr.pdf --out out/refs
uv run python -m tools.emulator.ready dt2 --syx firmware/Digitakt_II_OS1.17.syx
```

- Drive it with `tools/emu_drive.py` (this repo): load, tap, combo (FUNC+AMP), burst (encoders), shot, log.
- **Timing:** 132,000 ColdFire instructions per emulated ms. Encoders need bursts of events about 2 ms
  apart; a single slow event moves a value by one raw step and the display may not change.
- **ColdFire-only mode does not run the sequencer clock.** PLAY lights up, but the playhead LEDs do
  not move. Fires seen there are not normal playback. Sequencer tests need `--dsp zero` or the
  coupled DSP, both from the `emu:ready` snapshot.
- Stock 1.17 boots to the main UI in about 850M instructions (about 8 s on this host). A patched
  build loads with `firmware.load {syx, start: cold}`; a patched file cannot be the daemon's startup argument.
- **Verified in the emulator:** core-dt2 plus hello-marker boots, and the screen differs from stock only
  in the marker's corner. FUNC + AMP opens the stock Euclid menu (PL1 PL2 LEN EUC / RO1 RO2 TRO OP).

## Probe results (mods/eucplus-dt2 0.0.1, ColdFire-only mode)

- The hook at `0x400d6e10` runs on track 1 with Euclid on (`+1176` = 1). The pattern argument is
  **`0x41776cb8`**, the same address digikit found as pattern slot 0 on 1.15C.
- Fire arguments (frame +56…): track, pattern, a per-track buffer (`0x426532b8`), step, a pointer
  (`0x44f37c94`), two output pointers (alternating between two buffers), `0x450439b0`.
- With the sequencer clock stopped, every fire reported step 0. Explained: the step is the playhead
  byte, which does not move without the clock. With the paced clock, steps run 0, 4, 7, 10, 13 for PL1 = 5.


## The fire's caller and the pattern (verified 2026-11-03)

- The caller at `0x400d81b0` pushes, per track: track, **the active pattern (`*0x4316501c`)**, a
  record (`*0x43165028`), **the playing step (a per-track playhead byte)**, 0 (the fire allocates
  the event), d6, and an out pointer. `0x43165024` looks like the next pattern, as on the mk1.
  The playing pattern was `0x41776cb8` in every run.
- In Euclid mode the step the fire reads is the **source step = the hit's index in the pass**
  (0, 1, 2…), so generated hits use successive source steps and their locks.
- Track record (stride 1701): **VEL `+1152`, default trigs `+1154` (u16), NOTE `+1157` (60 = C5),
  PROB `+1168`, Euclid flag `+1176`**. Per-step bytes (128 steps): tables at `+256`, `+384`, `+512`,
  `+640`, `+768`, `+896`, `+1024` (conditions use 640/768/896).
- The pattern's lock index: s16 at `pattern + 2·(38184 + 128·track + step)`, −1 = no locks.
- **Event:** byte `+38` = NOTE, `+39` = VEL, `+40` = `+1153`. These are written by the fire when the
  step has no locks; that is the byte EUC+ sets. The MIDI fire's event (fp) has the same bytes.
  Confirmed end to end: the notes on USB MIDI equal the bytes EUC+ wrote.
- **KB SCALE / ROOT NOTE are per pattern**, not per track: s8 at `pattern + 121725` (−1 chromatic,
  then App. D order from 0 = Ionian) and u8 at `pattern + 121727` (0 = C). Changing them on T1 or T2
  changes the same bytes.
- The firmware's 7-note mode tables (Ionian … Locrian bb3 bb7) are at `0x4022492c`, with 255 for
  scales that do not have 7 notes. The scale names are at `0x402b6830`.
- A MIDI track (MIDI machine, CHAN enabled with FUNC + push A) sends to **USB** in the emulator's
  default config, not DIN.

## Testing with a running sequencer

ColdFire-only mode has no sequencer clock. Use the paced mode:

```sh
digi-emud firmware/Digitakt_II_OS1.17.syx --dsp zero \
    --cf-snapshot out/snapshots/dt2-audio-ready-cold-*/digi-audio-m5.snap --run-dir out/native/run --no-audio
python3 -I tools/unpack.py ELEKLOADER build.syx build.main.bin
uv run python tools/hotpatch.py out/native/run stock.main.bin build.main.bin build.syx.map.json
uv run python mods/eucplus-dt2/test/e2e_emulator.py out/native/run build.syx.map.json
```

- A cold `firmware.load` of a patched image drops the session to `dsp: none`, so `tools/hotpatch.py`
  writes the patched bytes into the running paced session instead. It does what core's boot copier
  does: copies `__run_load` to `__run_start` and zeroes `.bss`.
- `emu:ready` needs digikit's shared SHARC core (`sharc_transpile --strict --out out/native/shared-core/gen`,
  then `SHARC_GEN_DIR=…/shared-core/gen`) and the HWR text above.
- Do not `pkill -f digi-emud` from a shell whose own command line contains that string. Use the
  control socket's `quit`.

## v0.2: the UI (verified in the emulator, 2026-11-03)

All of these were found or checked in the paced emulator with the EUC+ build hot-installed, and
the EUC+ end-to-end test (`tools/run_e2e.sh`) exercises each of them.

**Events (core-dt2 1.0).** `ev_key`: `ev+12` the key id, the same numbers as the emulator's button
codes (FUNC 17, AMP 4, NO 12, LEFT 13, RIGHT 15, YES 10, trig 1-16 = 25-40, encoder pushes 41-48),
`ev+16` the flags: 1 press, 2 FUNC held, 0x10 release (FUNC + AMP comes as FUNC 1, AMP 3, AMP 0x12,
FUNC 0). `ev_enc`: `ev+12` the encoder 1-8 (A-H), `ev+16` the signed delta. The driver merges
events: ten quick +1 detents arrive as one +10. One emulator `turn` is at most 30 detents.
**[V]** by logging every event to a ring and reading it back.

**The stock SEQUENCER page and FUNC + AMP.** FUNC + AMP opens the page (title "Euclidean"). On that
page a second FUNC + AMP changes nothing: the frame hash is identical after it, twice. NO does not
leave the page either (it is a page, not a menu). The page keys (SRC, FLTR, AMP, FX, MOD) leave it.
**[V]** frame hashes and screenshots.

**Telling that the page is on screen.** The frame's title bar is no use: after a parameter edit it
shows the parameter ("Pulses Gen 1: 5") and stays so, and with EUC on it reads "Euclidean mode...".
The two rows of parameter labels are fixed: `PL1 PL2 LEN EUC` and `RO1 RO2 TRO OP`. EUC+ hashes
them in `ev_draw` (rows 0-4 and 27-31, columns 18-127; djb2-xor over the column bits): `0xf6f7e0f0`
on the page, for audio and MIDI tracks, EUC on and off, after edits, on other tracks, playing or
stopped; a different value on the main screen, on FLTR, AMP, FX and MOD, and on SRC with the
sample machine and with the MIDI machine. **[V]**. The firmware's view object for the page was not found:
`ctrl` (`ev_draw`'s second argument) has no field that tells pages apart, and `Brain::key`
(0x40030bd0) passes keys to a listener list and then to the view manager at `brain + 64`
(0x4011c79c), whose tree was not followed.

**The frame Bitmap.** `+0` vtable `0x4021cff8`, `+4` width 128, `+8` height 64, `+12` longs per
column (2), `+16` the pixels. Column x, row y (0 = the bottom row) is bit `31 - y % 32` of long
`x * stride + y / 32`; from `Bitmap::fillRect` (0x401131ae): its masks are `-1 >> (y0 & 31)` and
`-1 << (~y1 & 31)`, its word `x * stride + y / 32`. The OLED frame digikit reports is the same
bytes (`frame[(7 - y/8) + 8x]`, y from the top). There are two frame buffers (0x44e4a5b8,
0x44e4a9b8), used in turn; `ev_draw`'s Bitmap is a temporary object, so do not keep it. **[V]**

**Text.** `0x40113d84` is `textf(bmp, font, x, y, maxlen, fmt, ...)`: `vsprintf` (0x400005e0) into
a 128-byte buffer, then `0x40113d60`. `0x40113d60` is `drawstr(bmp, font, x, y, maxlen, s)`
(it calls 0x40113a84 with one more argument, 0). It ORs the text in, with y its bottom row, and
**returns the width drawn in pixels** ("PTCH" 19, "I" 2, "W" 6, "ABCDEF" with maxlen 2: 10). EUC+
measures text by drawing it into a 128 x 32 Bitmap of its own (the frame's header, its own
pixels). Found from the mk1: there `fillrect` 0x400c19a6, `textf` 0x400c257c and `blit` 0x400c2960;
the DT2's `fillrect` (0x401131ae) and `blit` (0x40114168) are the same 0xfba apart, and
0x401131ae + 0xbd6 (the mk1's `textf - fillrect`) is 0x40113d84, which has the `textf` shape.
**[V]** by drawing.

**Fonts** (pointers the stock pages pass to `textf` / `drawstr`; counted over every caller):
`0x402aa570` is the 5-pixel capitals of the stock labels ("PL1"), header `{5, 5, ...}`;
`0x402a9be0` is a 6-pixel upper- and lower-case font, header `{3, 0x10006, ...}`. **[C]** (see
"v0.2 screens" below): `0x44d2e6f4` is a font *object*, the narrow font; it drew nothing with
`drawstr`, but draws with `textf`. **[V]** by drawing.

**The active track.** `0x40077308` is the track select (TRK + trig): it clamps the track to
0-15, gets `ctx = 0x40198b7e()` (a lazily made singleton, `*0x44f370a0`), `sel = 0x40041d18(ctx)`
(`ctx + 48`), and compares the track with `0x4004abb8(sel)` before it calls the setter 0x4004e3ca.
`0x4004abb8` reads `sel->vtbl[10](sel) + 8`. So **the active track is
`0x4004abb8(0x40041d18(0x40198b7e()))`**, 0-15. These return pointers **in d0**: in C, declare
them as returning an integer, or GCC (m68k-linux) reads a0, which crashed the emulator
(`UnsupportedGuestPc(0)`). Found by snapshot diffs over track changes (six bytes followed the track:
0x41765c9f, 0x41765cf0, 0x42936825/9, 0x45139c6d/71), then write guards on them (`guard.set`),
whose writer at 0x4004e41e led to the setter and its caller. Call them from the UI task only.
**[V]** by switching tracks (TRK + T2 on the EUC+ page edits track 2).

**The transport.** `0x43164f94` (s32) is the sequencer's run state: **0 stopped, 1 playing,
2 paused** (PLAY while playing). `0x400d688a` returns `*0x43165010 | *0x43164f94`. Found by
snapshot diffs stopped / playing / stopped / playing (0, 1, 0, 1), then its 13 users, all in the
sequencer (0x400d5d86 and 0x400d6650 set 1; 0x400d673c, 0x400d79ba clear it). **[V]** by peeking it
through PLAY, PLAY (pause), STOP. A plain read is safe at interrupt level.

**STOP builds the next PLAY's first event.** On STOP the sequencer runs one more fire, for step 0,
while the run state is already 0, and PLAY sends that event at once (no new fire for step 0). This
was the v0.1 test's "first note queued before PLAY". EUC+ treats a fire while stopped as the
riff's first note. **[V]**: the EUC+ log gets one record at STOP (step 0) and none at PLAY, and
with EUC+ on the first note sent after PLAY is the riff's first.

## v0.2 screens: stock text and layout (verified in the emulator, 2026-11-03)

Leads from toonst/digi_macro and digikit's finding 71 (docs/EXTENDED-EUCLID.md, "Prior art"),
checked here by drawing from `ev_draw` and reading the frame:

- **`textf` `0x40113dc8(bmp, font, x, y, flags, fmt, ...)`** draws with the stock **font objects**
  and returns the width drawn. `y` is the text's bottom row. Flags: 2 centres on `x`, 4 right-aligns
  to `x`, `0x18` draws the text dark in a white box one pixel bigger.
- **Font objects** (RAM, `0x18` apart), as drawn with `textf`:

  | Address | Font |
  |---|---|
  | `0x44d2e6ac` | 16 pixels, bold |
  | `0x44d2e6c4` | 8 pixels |
  | `0x44d2e6dc` | 6–7 pixels, bold |
  | `0x44d2e6f4` | the narrow font, 3-pixel letters, lower case too (the stock page box and lists) |
  | `0x44d2e70c` | the stock label font, 5-pixel capitals (lower case as small capitals) |

  `0x44d2e724` and the next three draw nothing. They are RAM objects the firmware builds at boot;
  digi_macro uses `0x44d2e70c` and `0x44d2e6f4` on a unit, so their addresses held across boots
  there.
- **`0x402a9be0` is a font record, not an object:** `textf` draws nothing with it, and `drawstr`
  (`0x40113d60`) draws it. EUC+ titles its bar this way.
- **The stock parameter page's geometry** (FLTR, dumped; rows from the top):
  - header rows 0–9 (pattern box `x 0–14`; title bar `x 17–127`, rows 0 and 9 cut to `x 18–126`);
  - track box `x 0–14`, rows 13–36, corners cut;
  - LEV fader `x 0–14`, rows 40–56, label at 59;
  - labels at rows 32–36 and 59–63, centred on `x 33, 59, 85, 111`;
  - knobs of radius 8 centred on the same columns, rows 21 and 48, with a pointer from the
    centre (ENV.D at 111, 48).
- **Bitmap:** EUC+ reads `ev_draw`'s Bitmap pixels at `+0x10` (its page signature depends on it and
  works). digikit's finding 71 reads the screen Bitmap's data at `+0x14` (ctor `(this, w, h, buf,
  buf2)`, vtable `0x4021cff0`). The frame `ev_draw` gets has vtable `0x4021cff8` and a second
  pointer at `+0x14` (`0x40966ad8`). They are probably two classes; not followed.

## Audio tracks do not repitch from EUC+'s note (verified with the real DSP, 2026-11-05)

> **Contradicted** by a later measurement on a Mac the same day (next section): there the audio track
> did repitch to EUC+'s notes. The cause of the difference is not known; this section is kept as
> the evidence it was.

Tested in digikit's coupled emulator (ColdFire and the real SHARC DSP), with a +Drive card holding a
1 kHz sine (digikit's `dt2_sample_check` card; the card's project plays it on track 1), build E201
loaded as a file (a coupled cold boot, the DSP kept), track 1 an audio track with EUC on (hits at
steps 0, 7, 14). Pitch measured by FFT of the captured PCM (`pcm.tap capture`):
- **Control:** the track's NOTE set to 64 (poked at `+1157`): the trig sounds at 1260 Hz (all the
  energy there), against 1000 Hz at NOTE 60. The method sees a repitch.
- **EUC+ off:** 1000 Hz throughout; EUC+'s log: note 60 → 60.
- **EUC+ on** (SCL IONI, ROOT C): EUC+'s log shows notes 59, 60, 62, 64 written to the event's
  `+38`, but the sound stays at **1000 Hz in every window**; energy at 944 / 1122 / 1260 Hz about
  0.02 / 0.006 / 0 of that at 1000 Hz.

**So on audio tracks the voice's pitch does not come from the event's `+38` as it stands after the
fire.** It comes from the track's NOTE by another path. On MIDI tracks `+38` is what is sent (the
e2e test). The fire copies `+1157` into `+38` at `0x400d6d3c` (audio) and `0x400d71ea` (MIDI). Other
readers of `+1157`: `0x4002e3a4`, `0x40053db6`, `0x40058c32`, `0x4012cd6a`, `0x4013a15e` (not yet
followed). **Open:** find what the audio voice's pitch is computed from, and where EUC+ can set it.

## Audio tracks do repitch from EUC+'s note in digikit's desktop app (verified with the real DSP, 2026-11-05)

First noticed by ear by the user, then measured. Setup (tools/emulab/NATIVE-MAC.md, as corrected in
`8ee96d2`): digikit `9a0ddda`'s desktop app (`tools.emulator.native_emu --no-runtime`, live, the fast
DSP core `out/native/dt2-audio/gen` from `aot_gen dt2`), started on stock 1.17 with a +Drive folder
holding only digikit's `test-sine-1khz.wav` (48 kHz mono, 1 s; the card's ready state
`by-card/1c1fed346ea9`), then **E201** loaded with `firmware.load {start: cold}` (status:
`syx_sha256 a17115e8…`, `runtime: null`; USB SoftwareVersion `E201`). Apple Silicon, macOS 26.6.2.

Track 1, an audio track playing the sine, EUC on, the pattern playing (run state `0x43164f94` = 1).
EUC+ NOTES page: PTCH 0, RNGE 0, PHRS RAND, SCL **PMIN**, ROOT FLW; RANDOM RAMT 0. The ON switch (H)
was turned through the panel encoder (`input.turn`). PCM captured with `pcm.tap capture`, left channel,
cut into 4096-sample windows (85 ms; FFT bins 11.7 Hz apart), silent windows (RMS < 0.003) dropped,
the peak bin of each window counted:
- **EUC+ OFF** (6 s): 996 Hz in all 59 windows. The stock note (1 kHz) only.
- **EUC+ ON** (8 s, the same pattern straight after): 996 Hz in 46 windows, **1336 Hz** in 17,
  **1781 Hz** in 17.
- Expected for the pool 1-3-5 in pentatonic minor (0 3 5 7 10) with every movement at 0: degrees 0,
  2, 4 = +0, +5, +10 semitones = 1000, 1335 and 1782 Hz. The measured peaks are those three, within
  one bin. (The 1 s sample overlaps the next hits, so the root's windows are not a count of hits.)
- After switching H back OFF, the screen reads OFF again.

So on this setup **the audio voice takes its pitch from the event's `+38` as EUC+ leaves it**. The
earlier result (previous section) was a different setup: digikit's coupled emulator in the Linux
cloud session, E201 "loaded as a file (a coupled cold boot, the DSP kept)", NOTE poked at `+1157` for
the control, SCL IONI and ROOT C. Not known which difference matters; candidates: how E201 was
booted, the DSP core in use, how the EUC+ settings were set, and the scale. **Not yet checked:** the same measurement with SCL IONI / ROOT C, an
audio track other than track 1, and the unit itself (E201 test list, step 9). Until one of those
settles it, treat audio repitching on E201 as **unconfirmed, likely working**.

## The trig LEDs in Euclid mode (verified in the emulator, 2026-11-04)

Read with digikit's `leds.get` (one byte per LED; index = digikit's `devices/digitakt-ii-leds.toml`
`led`: TRIG n = 24 + n, RECORD 9; value = colour bits 4-7, mode bits 0-3, `0x01` off), track 1 with
EUC on and PL1 = 5:
- **Grid RECORD off:** the trig keys show no hits, only the selected track's key (`0x06`) and, while
  playing, the playhead (`0x11` red; `0x71` on a hit). The same on the Euclid, TRIG and SRC pages.
- **Grid RECORD on:** RECORD and the generated hits light `0x85` (bit 7, mode 5): TRIG 1, 5, 8, 11 and
  14, the steps the generator makes (0, 4, 7, 10, 13). This is the manual's "With the mode on, [REC]
  and the generated trigs light blue". `0x85` is the colour bit digikit's table had not seen on the
  wire; on the unit it is presumably the blue the manual names.
- **With EUC+ open and ON** the LEDs are byte for byte the same, stopped and playing: EUC+ leaves
  the pulse generator and its hits alone. It changes only the notes the hits play.

## Build checks and facts from Aggregat (2026-11-03)

[Aggregat](https://github.com/70b185/Aggregat) collects the community's rules and device facts. It is
second-hand; the facts below are marked as its sources mark them, and nothing in this section is
used by EUC+ except the two checks.

- **ColdFire encodings [V]:** every instruction in `eucplus.c.o`, `ui.c.o` and `site.s.o` is at most
  6 bytes (`tools/check_build.py`, run by every build). Why it matters (Aggregat A42, `[hw]`): ColdFire
  instructions are 2, 4 or 6 bytes. 68000 forms such as `move.l #imm,d16(An)` (8 bytes) and
  `move.l #imm,abs.L` ran in the emulator and crashed or hung real units. Our compiler and assembler
  target the ColdFire (`-mcpu=54455`); the check catches hand-encoded bytes.
- **No firmware bytes in the `.elemod` [V]:** `check_build.py` finds no 16-byte run of the `.elemod` in
  the stock main OS. Its control, stock bytes against stock, finds runs. The linked image does
  repeat stock bytes in 10 places: the scale names ("Ionian", "Harmonic Min", "MIXO AEOL ...", as the
  firmware spells them at `0x4025db0d`), and EUC+'s calls of `0x40041d18` / `0x4004abb8`, compiled
  the same as the firmware's own (`0x4001e2f2`). Elekloader stores such runs as references to the
  user's file, so they are not in the `.elemod` (Aggregat B7).
- **A ColdFire fault halts the unit until power is cycled** `[doc]` (digikit finding 37): the default
  exception handler prints `EXCEPTION` (vector, format, PC, stack words) and halts with interrupts
  masked; the watchdog is unused.
- **Recovery has been used on a Digitakt II** `[hw]` (dt2-machine-lab, 2026-10-06, OS 1.16): the
  startup menu over DIN MIDI from a known-good interface restored the unit after a crashing build.
  A generic USB-MIDI cable adapter (VID 1a86, PID 752d) delivered nothing. A second source
  (own-dt2-notes, `[hw]`) used the same recovery throughout its SHARC work, and reports bootstrap
  0x0201 on both 1.16 and 1.17.
- **Screen capture over SysEx** `[doc]` {elek-view}: a screen request in the `F0 00 20 3C 10 00` RPC
  framing (Elek View, a browser port of screendump) works on a Digitakt II.
- **Memory** `[emu]` (digitakt-ii-research and dt2-machine-lab, DT2 **1.16**; resolve on 1.17 first):
  - the OS clears `0x40312000-0x47e28470` at reset;
  - the firmware heap is a 32 MiB buddy heap at `0x44f39c40`;
  - `malloc` is `0x4011e674`, `free` is `0x4011e87a`;
  - the flash cave `0x403117c4-0x40312000` survives boot and has five users;
  - Elekloader's RAM `0x47f00000-0x47f40000` lies inside digikit's `cf.expansion`, so EUC+ does not
    combine with digikit's runtime.

**Not working in this emulator:** `mem.cf.peek` reads SRAM (0x80000000-0x80010000) as 0. A
`mem.cf.poke` into firmware code or data at 0x40800000 crashed the session (do not poke markers
into unknown memory; `state.save` plus `tools.emulator.snapread` reads all memory, by 1 MB pages).

## Still to find

1. ~~The fire's caller and the note site~~ (done, above).
2. The lock list layout (`pattern + 27216`, 14-byte records?) for writing a note into locked steps.
3. ~~KB SCALE / ROOT~~ (done: per pattern).
4. The retrig lock slots.
5. The SEQUENCER (Euclid) view object (v0.2 uses the page's label pixels instead, above).
6. ~~The transport state~~ (done: `0x43164f94`, above).
