# Digitorsion: plan

Extending the Digitakt II's Euclidean sequencer with **EUC+**, a Torso-T-1-style layer (in-scale
generated notes, phrases, lockable random, later groove and voicing). It is entered from the stock
Euclid menu and never replaces the stock mode.

Last updated: 2026-11-03. Target: Digitakt II **OS 1.17** only.

## Where things stand

| Version | Scope | Status |
|---|---|---|
| research | Stock Euclid, Torso T-1, modding ecosystem, firmware layout | done: [docs/EXTENDED-EUCLID.md](docs/EXTENDED-EUCLID.md), [docs/RESEARCH-LOG.md](docs/RESEARCH-LOG.md) |
| **v0.1** | Note engine: pool (1-3-5), phrase × range, seeded lockable random, the pattern's KB SCALE / ROOT; audio and MIDI tracks; **off by default** | **done, emulator-verified** (`mods/eucplus-dt2`, end-to-end test PASS). Not yet run on hardware. No UI: settings in RAM |
| **v0.2** | The EUC+ pages, opened from the stock Euclid menu; per-track on/off; phrase reset on PLAY | **next** (task list below) |
| v0.3 | GROOVE page (prob, accent shape, repeats via retrig) + saving the settings | planned |
| v0.4 | VOICE page (MIDI chords, arpeggio), pool keyboard on the trig keys, HARM, COMMIT to p-locks | planned |
| later | `seq-events-dt2` (a general step-fire API for Elekloader, upstream PR); Modwerk hook proposal | optional |

## Route (decided)

- **Elekloader linkable mod** (`mods/eucplus-dt2`) on **core-dt2 1.0**, built against your own stock
  OS file. Elekloader re-seals it, and the bootloader is never touched.
- **Hooks:** two 6-byte `jsr` sites at the end of the step fires (audio `0x400d6e10`, MIDI `0x400d7234`).
  EUC+ rewrites the note byte of the event the fire built, only on Euclid hits without p-locks.
  It never writes pattern data, so projects stay stock-compatible.
  - This replaced the earlier idea of porting Elekloader's whole `seq-events` layer first. Direct
    hooks were enough for v0.1. The port is still worth doing later, for upstream.
- **Test before hardware:** digikit's emulator with a paced sequencer clock (`--dsp zero`). Builds
  are hot-installed into it (`tools/hotpatch.py`). `mods/eucplus-dt2/test/e2e_emulator.py` checks the
  notes the Digitakt actually sends over USB MIDI.

## Starting a new session

1. `bash tools/setup_env.sh` (about 10 min). It installs the m68k toolchain, the pinned Elekloader
   and digikit checkouts, and the stock OS 1.17 from elektron.se (hash-checked). It also builds the
   emulator and its ready state, and builds core + EUC+. Everything goes to `/tmp/digitorsion-work`,
   outside the repo.
2. `bash tools/run_e2e.sh`: the end-to-end test must print `RESULT: PASS`.
3. Edit `mods/eucplus-dt2`, rebuild with `bash tools/build_eucplus.sh`, and test again.

Reference: [docs/RESEARCH-LOG.md](docs/RESEARCH-LOG.md) has every firmware address and how it was
found. `tools/emu_drive.py` drives the emulator by hand (keys, encoders, screenshots, logs).

## v0.2 tasks

Goal: you can open EUC+ from the stock Euclid menu, switch it on for a track, and edit the NOTES
and RANDOM parameters on the device. Settings may still reset at power-on (storage is v0.3).

1. **Know when the stock Euclid (SEQUENCER) menu is open.** core-dt2 1.0 has no `core_view_top`.
   Either track it from `ev_key` (FUNC + AMP opens it; NO and the page keys close it) or find the
   view object in the firmware. Start with key tracking, and check it against screenshots.
2. **Entry gesture:** a second FUNC + AMP while the Euclid menu is open opens EUC+. NO, or FUNC +
   AMP again, goes back. First confirm in the emulator that stock does nothing with that press.
   The fallback is FUNC + RIGHT.
3. **Drawing:** an `ev_draw` overlay of the 128×64 frame in the stock page style: a title, 8
   parameter slots with labels and values. `Bitmap::fillRect` is at `0x401131ae`. **Find the
   firmware's text and font routine** (start from how stock pages draw labels such as "PL1").
4. **Input:** `ev_enc` takes encoders A–H while EUC+ is open (consume the event). Use the stock
   feel for acceleration. LEFT / RIGHT change page.
5. **Pages for v0.2:**
   - **NOTES:** PTCH, RNGE, PHRS, PLEN, (HARM later), SCL (FOLLOW or one of 36), ROOT (FOLLOW or
     C…B), and **ON**, the per-track on/off.
   - **RANDOM:** VAR, SEED, LOCK (and RAMT, the random amount on pitch).
   - Decision pending from the user: an ON control on the page (recommended), or opening the page
     switches EUC+ on.
6. **Phrase reset on PLAY:** find the transport state or a playback-start event, and reset each
   track's pass counter so a riff always starts from its first bar.
7. **Tests:** extend `e2e_emulator.py` to drive the real UI. It should open EUC+, switch it on,
   change PTCH and SCL, and check the MIDI notes, plus screenshot checks of each page. Keep
   `test/host_test.c` passing.
8. **First hardware check (by you, optional at v0.2):** flash, check recovery over 5-pin DIN MIDI
   first, then check by ear that audio-track repitching works.

## What I need from you

- **Hardware tests** when a build is ready: flash with Elektron Transfer and report back (photos of
  the screen help). **Before the first flash, check you can recover:** hold FUNC at power-on,
  press TRIG 4, and send the stock `.syx` over **5-pin DIN MIDI**. USB does not work for recovery.
- **Decisions:** the v0.2 ON-control question above, and later the storage location (per pattern or per kit).

## Risks

- **OS lock-in:** all addresses are for 1.17. A new Elektron OS means re-finding the sites (the
  method is in the research log).
- Hooks run at interrupt level, so the code stays small and allocation-free.
- Brick risk is low (bootloader untouched, DIN-MIDI recovery), but not zero.
- Since 16 Sep 2026, Elektron does not allow mod files or links on Elektronauts. Share elsewhere,
  as GPL-2.0 source only, never firmware.
