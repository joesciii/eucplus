# EUC+ for the Digitakt II

**EUC+** is a mod for the Elektron Digitakt II (OS 1.17) that adds generated melodies to the stock
Euclidean sequencer, in the spirit of the Torso T-1. Each Euclid hit gets a note picked from a pool of
scale degrees, moved by a phrase shape and a seeded, lockable random sequence, in the pattern's own
KB SCALE and ROOT NOTE. It has its own pages on the device, opened from the stock Euclid page.

It is **one mod**, not a custom firmware. It is an [Elekloader](https://github.com/irpina/elekloader)
linkable mod: you link it onto **your own** official OS file, alone or alongside other mods.

> **Status: tested in an emulator only.** The first hardware build (E201) has not yet been confirmed
> on a unit. Settings reset at power-on (saving them is the next version). Use at your own risk, and
> read [Safety](#safety) first.

## What it does

- **Off by default on every track.** Stock Euclid keeps its normal behaviour until you switch EUC+ on
  for a track with its own ON control. Opening the EUC+ pages never switches it on.
- **Never rewrites your patterns.** EUC+ changes only the note of each Euclid hit as it plays. Your
  project files stay plain stock projects.
- **NOTES page:** pitch (transpose within the scale), range, phrase shape and length, scale and root
  (the pattern's own by default, or any of the Digitakt II's 35 scales), and ON.
- **RANDOM page:** variance, seed, lock and amount. Locked, the riff repeats exactly; unlocked, it
  drifts a little each pass.
- Works on audio tracks (the sample is repitched) and MIDI tracks. Hits with parameter locks keep
  their stock note.

How to use it: [mods/eucplus-dt2/README.md](mods/eucplus-dt2/README.md).

## Getting it

This repository contains **no Elektron firmware**, and never will. You build the installable file
yourself from your own official `Digitakt_II_OS1.17.syx` (sha256
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`):

```sh
bash tools/setup_env.sh        # toolchain, Elekloader, the stock OS from elektron.se, the emulator (~10 min)
bash tools/build_eucplus.sh    # -> /tmp/digitorsion-work/builds/eucplus.syx
bash tools/run_e2e.sh          # the emulator test; must print RESULT: PASS
```

The scripts are written for a Linux machine (they install packages with apt). There are no prebuilt
files yet.

## For custom-firmware builders

EUC+ is a standard Elekloader mod (format 2) in [mods/eucplus-dt2](mods/eucplus-dt2), described by its
[mod.json](mods/eucplus-dt2/mod.json). To combine it with other mods, check that nothing else uses:

| What | EUC+ uses |
|---|---|
| Requires | `core` (core-dt2 1.0) |
| Hook sites (6-byte `jsr`, OS 1.17) | `0x400d6e10` (end of the audio step fire), `0x400d7234` (end of the MIDI step fire) |
| core events | `ev_tick`, `ev_key`, `ev_enc` (order 50), `ev_draw` (order 60) |
| Keys | a second FUNC + AMP while the stock SEQUENCER (Euclid) page is open; while EUC+ is open, encoders A–H, LEFT / RIGHT, NO, UP / DOWN, YES and the encoder pushes |

Every firmware address the C code uses is in [os117.h](mods/eucplus-dt2/os117.h); the sites are in
[site.s](mods/eucplus-dt2/site.s) and `mod.json`. EUC+ has not yet been tested together with other
mods.

## Safety

- **Rehearse recovery before your first flash**: hold FUNC at power-on, choose TRIG 4 (OS upgrade;
  never TRIG 2 or 3, which erase data), and send the stock 1.17 `.syx` over **5-pin DIN MIDI** from a
  known-good interface. The bootloader is never modified by Elekloader mods.
- Back up your projects and samples with Elektron Transfer first.
- The full procedure, and what to try on each build: [PLAN.md](PLAN.md), "Flashing a test build".
- To go back to stock, install the official OS file with Transfer as usual.

## Repository

- [mods/eucplus-dt2](mods/eucplus-dt2): the mod (`eucplus.c` the note engine, `ui.c` the pages,
  `os117.h` the firmware addresses, `site.s` the hooks, `test/` the host and emulator tests).
- [docs/EXTENDED-EUCLID.md](docs/EXTENDED-EUCLID.md): the research and the design.
- [docs/RESEARCH-LOG.md](docs/RESEARCH-LOG.md): the firmware facts EUC+ relies on, and how each was
  found.
- [PLAN.md](PLAN.md): status, roadmap and the hardware test procedure.
- [tools](tools): build, test and analysis scripts; [tools/emulab](tools/emulab): run the emulator
  with EUC+ in a browser or natively on a Mac.

## Credits

- [Elekloader](https://github.com/irpina/elekloader) and core-dt2, by irpina and contributors: the
  mod format, the linker and the event layer EUC+ runs on.
- [digikit](https://github.com/m-dwyer/digikit), by m-dwyer and contributors: the Digitakt II emulator
  every EUC+ test runs in, and its firmware research.
- [Aggregat](https://github.com/70b185/Aggregat): the community's rules and device facts, behind the
  build checks and the flash procedure.
- The other Digitakt II / Digitone II mods surveyed in
  [docs/EXTENDED-EUCLID.md](docs/EXTENDED-EUCLID.md), section 5.

## Licence

GPL-2.0-or-later ([LICENSE](LICENSE)).

Not affiliated with or endorsed by Elektron or Torso Electronics. Digitakt and Transfer are trademarks
of Elektron Music Machines MAID AB; T-1 is a product of Torso Electronics.
