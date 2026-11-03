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
- The permission check allowed running Elekloader's unpacker after the user's go-ahead. It
  **blocked** `uv sync` in digikit ("code from external"), which the emulator needs. It needs an
  explicit permission rule.

## The Digitakt II step fire (OS 1.17) [D]

Found by masked-opcode matching of the mk1's fire (`0x4006f4be`, the seq-events-dt1 sites) against
the DT2. 76 of 446 six-instruction shingles fall in `0x400d6800`–`0x400d6c00`.

| | mk1 1.54 | DT2 1.17 |
|---|---|---|
| audio fire, entry | `0x4006f4be` | **`0x400d6982`** (`lea -52(sp)`, `movem d2-d7/a2-a6`) |
| MIDI fire, entry | `0x4006f846` | **`0x400d6e88`** (same shape) |
| direct callers | — | none: reached through a pointer (to find) |
| track record stride | 911 (`d3 = 911·track`) | **1701** (`mulsl #1701`) |
| args after prologue | step `76(sp)`, event `80(sp)` | track `56(sp)`, pattern `60(sp)`, step `68(sp)`, event `72(sp)` |
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
- With the sequencer clock stopped, every fire reported step 0. To recheck with a running clock.


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

## Still to find

1. ~~The fire's caller and the note site~~ (done, above).
2. The lock list layout (`pattern + 27216`, 14-byte records?) for writing a note into locked steps.
3. ~~KB SCALE / ROOT~~ (done: per pattern).
4. The retrig lock slots.
5. The SEQUENCER (Euclid) view object, for `core_view_top` / the EUC+ entry gesture.
6. The transport state (PLAY from the top), to reset the phrase phase.
