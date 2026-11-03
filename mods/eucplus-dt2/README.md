# EUC+ v0.1 (Digitakt II OS 1.17)

T-1-style generated notes on top of the stock Euclidean sequencer. It is an Elekloader linkable mod
(format 2), built on `core` (core-dt2 1.0).

**Status: emulator-tested, not yet run on hardware. There is no on-device UI yet (that is v0.2).**

## What it does

**EUC+ is off on every track by default.** Stock Euclidean mode keeps its own note behaviour
(every hit plays the track's NOTE, or its p-lock) until you switch EUC+ on for that track. In v0.2
that happens when you enter EUC+ from the Euclid menu; in v0.1 it is the `on` byte in `eup_cfg`.

Once EUC+ is on for a track whose stock Euclidean mode is also on (FUNC + AMP, EUC), each generated
hit on a step with no parameter locks gets a note chosen by EUC+ instead of the track's NOTE:

- **Pool:** scale degrees to pick from, default 1-3-5 (bit mask `0x15`).
- **Scale and root:** the pattern's own **KB SCALE / ROOT NOTE** (FUNC + KEYBOARD), all 36 scales.
  CHROMATIC allows every semitone.
- **Phrase × RANGE:** melodic movement over PLEN pattern passes. Shapes: RAND, SAW, TRI, SINE,
  PULSE, CAD1–4.
- **Random:** a 16-step random sequence per track, from SEED. **LOCK** (default on) keeps the riff
  exactly. Unlocked, **VAR** sets how much it changes each pass.
- **Note range:** audio tracks stay within NOTE 36–84 (the sample is repitched). MIDI tracks
  (any track with the MIDI machine) send the note.
- Steps with p-locks, tracks without Euclid, and everything else play exactly as stock.

It hooks the end of the two step-fire functions (audio `0x400d6982`, MIDI `0x400d6e88`). It writes
only the event's note byte (`+38`) and never touches pattern data, so projects stay stock-compatible.

## Settings (v0.1: RAM only, `eup_cfg`)

`eup_cfg` = `{u32 'EUC+', u8 enable, pad[3], track[16] × 12 bytes}`. Each track entry is
`on, pool, ptch, rnge, phrs, plen, seed, lock, var, ramt, pad[2]`. The defaults are
`0 (off), 0x15, 0, 4, RAND, 2, 1, 1, 24, 3`. v0.2 adds the EUC+ pages (second FUNC + AMP in the
Euclid menu) and stores the settings.

## Build

```sh
# from an elekloader checkout; Ubuntu's m68k toolchain works
export ELEKLOADER_CROSS=m68k-linux-gnu-
python3 -m elekloader.sdk.build mods/core-dt2 --stock Digitakt_II_OS1.17.syx
python3 -m elekloader.sdk.build path/to/mods/eucplus-dt2 --stock Digitakt_II_OS1.17.syx
python3 -m elekloader.patch --stock Digitakt_II_OS1.17.syx \
    --mod mods/core-dt2/out/core-1.0.elemod --mod path/to/mods/eucplus-dt2/out/eucplus-0.1.0.elemod \
    --version EUP1 --out eucplus-v0.1.syx
```

## Tests

- `test/host_test.c`: the note logic on the host (`cc -O1 -o t test/host_test.c eucplus.c && ./t`).
- `test/e2e_emulator.py`: digikit's emulator with a paced clock. It sets up track 1 as a MIDI
  Euclid track (PL1 = 5), A pentatonic minor, and checks the USB MIDI it sends: stock notes with
  the defaults, then EUC+ switched on for track 1 (locked, then unlocked). Result
  (2026-11-03):

```
default:   [60, 60, 60, 60, 60, 60, 60, 60, 60, 60, 60] -> PASS stock Euclid notes
locked:    [60, 67, 69, 72, 79, 74, 64, 69, 81, 76, 74, 67, 69, 72, 79, 74, 64, 69, 81, 76, 74, 67, ...]
  in scale True, notes sent == notes EUC+ chose True, repeats every 2 passes True
unlocked:  [74, 64, 69, 81, 76, 76, 69, 69, 69, 76, 74, 74, 67, 76, 74, 64, 76, 74, 67, 81, ...]
  in scale True, riff changes True
RESULT: PASS
```

The first note of each run was queued before PLAY. In every case the build also cold-boots to
the main screen in the emulator.

## Known limits (v0.1)

- No UI and no storage: the settings reset at power-on.
- Steps with any p-lock keep their stock note.
- The phrase phase follows EUC+'s own pass count. It is not yet reset when PLAY starts from the
  top, so the first pass may start at either half of a 2-pass phrase.
- Audio-track pitch output is not yet verified by ear. The note byte is the one the firmware fills
  from NOTE, and on MIDI tracks the same byte is what is sent. Confirm on hardware.
