# Digitorsion: plan

Extending the Digitakt II's Euclidean sequencer with **EUC+**, a Torso-T-1-style layer (in-scale
generated notes, phrases, lockable random, later groove and voicing). It is entered from the stock
Euclid menu and never replaces the stock mode.

Last updated: 2026-11-03. Target: Digitakt II **OS 1.17** only.

## Where things stand

| Version | Scope | Status |
|---|---|---|
| research | Stock Euclid, Torso T-1, modding ecosystem, firmware layout | done: [docs/EXTENDED-EUCLID.md](docs/EXTENDED-EUCLID.md), [docs/RESEARCH-LOG.md](docs/RESEARCH-LOG.md) |
| v0.1 | Note engine: pool (1-3-5), phrase × range, seeded lockable random, the pattern's KB SCALE / ROOT; audio and MIDI tracks; **off by default** | done, emulator-verified |
| **v0.2** | The EUC+ pages (NOTES, RANDOM), opened from the stock Euclid page; per-track ON; own SCL / ROOT; phrase reset on PLAY | **done, emulator-verified** (end-to-end test PASS through the real UI). **Not yet run on hardware** (task 8). Settings in RAM |
| v0.3 | GROOVE page (prob, accent shape, repeats via retrig) + saving the settings | **next** |
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

## v0.2 (done, 2026-11-03)

You can open EUC+ from the stock Euclid page, switch it on for a track, and edit NOTES and RANDOM
on the device. How to use it: [mods/eucplus-dt2/README.md](mods/eucplus-dt2/README.md). How each
part was found: [docs/RESEARCH-LOG.md](docs/RESEARCH-LOG.md), "v0.2: the UI".

1. **Knowing the Euclid page is open:** neither key tracking (NO does not leave the page) nor the
   title bar (it shows the last parameter edited) works. `ev_draw` hashes the page's two label rows
   (PL1 PL2 LEN EUC / RO1 RO2 TRO OP), the same on audio and MIDI tracks and with EUC on or off.
   The firmware's view object was not found; the pixel check replaced it.
2. **Entry:** a second FUNC + AMP on that page (stock does nothing with it: identical frames).
   NO or FUNC + AMP goes back; page keys leave EUC+ for their page.
3. **Drawing:** the firmware's `drawstr` (0x40113d60, returns the width) and `textf` (0x40113d84),
   the stock label font (0x402aa570) and a 6-pixel title font (0x402a9be0).
4. **Input:** `ev_enc` takes A–H while EUC+ is open (the delta comes with the stock acceleration);
   LEFT / RIGHT change page. Transport, TRK, trigs, FUNC and LEVEL pass through.
5. **Pages:** NOTES (PTCH RNGE PHRS PLEN — SCL ROOT ON) and RANDOM (VAR SEED LOCK RAMT). ON is
   the per-track switch, off by default, never changed by opening or closing; the title bar shows
   ON (black box) or OFF on every page. The pages follow the active track
   (`0x4004abb8(0x40041d18(0x40198b7e()))`).
6. **Phrase reset:** the run state at `0x43164f94` (0 stopped, 1 playing, 2 paused). STOP then
   PLAY restarts the riff and the random sequence; pause and resume go on. The event STOP builds
   ahead for the next PLAY now gets the riff's first note.
7. **Tests:** `e2e_emulator.py` drives only keys and encoders and checks the MIDI notes and the
   screens (list in the mod README). `host_test.c` builds with `-DEUP_HOST`.
8. **First hardware check (you, optional):** still to do. Follow "Flashing a test build" below,
   then: FUNC + AMP twice on a Euclid track opens EUC+; ON changes the notes; by ear, that
   audio-track repitching works. Photos of the EUC+ page help.

Not done in v0.2 (moved on): the T-1 random gesture (FUNC + turn), COMMIT.

**v0.2 screens refresh (2026-11-03):** the pages now use the stock parameter pages' layout and the
firmware's fonts (stock header's pattern box, EUC+ title bar, page box, radius-8 knobs on the stock
grid, toggles, the value in the title and label while turning, the ON switch where LEV sits). From
a survey of the Digitakt II / Digitone II mods that draw UI (docs/EXTENDED-EUCLID.md, section 5).
Addresses moved to `os117.h`; a host UI test and `tools/host_tests.sh`; `-Werror` builds.

## v0.3 tasks

1. **Storage:** decide where the settings live (per pattern, preferred, or `core_kitdata`), find
   free bytes, and load / save with the project. Until then settings reset at power-on.
2. **GROOVE page:** PROB (skip a hit: clear the event), ACC / GRV (velocity byte `+39`), REP / RTIM /
   RAMP (find the retrig lock slots, research log "Still to find" 4).
3. The T-1 random gesture: hold FUNC and turn a parameter to set its random amount.
4. Keep the end-to-end test driving the real UI for each new parameter.
5. From the survey (docs/EXTENDED-EUCLID.md, section 5), in this order:
   - Tell the SEQUENCER page from firmware state: the top view's vtable and page number
     (`Brain + 0x40`, a page view's slot `+0x88`), replacing the pixel signature; step aside when a
     dialog is on top.
   - A stock-vs-EUC+ screen comparison in the e2e test (the same keys on both images, frames
     identical until the second FUNC + AMP).
   - A bit-exact engine regression check against a git revision.
   - A four-character OS version per hardware test build.
   - Later, optional: the firmware's own knob icons (tier B); EUC+ as a second stock SEQUENCER page
     (tier C, with storage).

## Flashing a test build

From the community's own incidents, as collected by
[Aggregat](https://github.com/70b185/Aggregat) (`hardware/AGENT-RULES.md`; its rule numbers in
brackets):

1. **Rehearse recovery with the stock file first** (A2, A11, A36). Hold FUNC at power-on, press TRIG 4
   (never TRIG 2 or 3: they erase data, A17), and send the stock 1.17 `.syx` from Transfer's SYSEX
   TRANSFER page over **5-pin DIN MIDI**. USB is not heard there.
   - Digitakt IIs have been recovered this way, over DIN from a known-good interface: once by
     dt2-machine-lab (OS 1.16), and repeatedly in one researcher's own SHARC work. That researcher
     also reports the bootstrap (0x0201) is the same on 1.16 and 1.17, so moving between them
     is reversible (Aggregat, `[hw]`).
   - A generic cheap "USB2.0-MIDI" cable (VID 1a86, PID 752d) delivered nothing. Use a known-good
     interface, and send a stock file through it once before you need it.
2. **Back up** projects, sounds and samples with Transfer (A3).
3. **One named build per flash, one change per build** (A12, G5).
   - I give you the file name, its SHA-256, the four-character version shown on the unit, and the list
     of what to try.
   - Keep the stock `.syx` and the last build that worked; never overwrite either.
4. **Never power off during the update** (A4). "Success" on screen is not proof (A23): check the
   version after the restart.
5. **If it hangs:** a ColdFire fault halts the unit until power is cycled, because the watchdog is
   unused (digikit finding 37). Note any `EXCEPTION` or `V04 M0 P...` text on the screen, power-cycle,
   and recover with step 1. After **two failed flashes on the same problem we stop** and re-plan (G6).
6. **Screens:** besides photos, [Elek View](https://github.com/BlueSGL/Elek_View) (a browser port of
   screendump, listed by Aggregat) reads the screen over USB SysEx and was tested on a Digitakt II.
   An exact capture of the EUC+ page can then be compared with the emulator's.
7. **Also check after a long power-off** (minutes, not a quick restart): one Octatrack change booted
   in the emulator and in quick power cycles, then stalled at the logo after minutes off (A40).

## Hardware builds

One row per build sent to a unit; never reuse a version. Build with
`EUP_OS_VERSION=<version> bash tools/build_eucplus.sh` and rename the file to
`Digitakt_II_OS1.17_EUCPLUS_<version>.syx`.

| Version | Source | SHA-256 of the `.syx` | What it is | Result |
|---|---|---|---|---|
| E201 | v0.2 as of 2026-11-04 (`e12515c` + build script) | `a17115e8fc870730fe354a069e2d83728380dd14f41447f466ddf2b8907e9406` | v0.2: the NOTES and RANDOM pages, per-track ON, phrase reset on PLAY | waiting. Audio-track repitch: **two emulator results disagree** (2026-11-05: none in the cloud; correct notes in the Mac desktop app; research log); MIDI tracks do |

**What to try on E201**, in order (stop at the first failure and note what the screen shows):
1. It boots to the main screen. The OS version reads E201. (Not shown in SETTINGS > SYSTEM in the
   emulator, where only the USB query found it; note where the unit shows it.)
2. A track you already use plays exactly as before. EUC+ is off by default.
3. On a Euclid track (FUNC + AMP, EUC on, PL1 = 5): FUNC + AMP again opens EUC+ (`EUC+ Notes (1/2)`).
   The notes are still stock. Use a track without p-locks on its grid (or clear its sequence first,
   then set EUC and PL1 again: FUNC + CLEAR resets Euclid's pulses to 0): hits that pick up a grid
   lock stay stock.
4. Turn H: the switch on the left fills, and the hits play new notes in the pattern's KB SCALE.
   Turn H back: stock notes.
5. Turn PTCH, SCL and ROOT while playing: the title shows each value, and the notes change.
6. RIGHT: the RANDOM page. LOCK off, then VAR up: the riff changes on each pass.
7. STOP, then PLAY: the riff starts from its first bar.
8. NO, and a page key (SRC): both leave EUC+ for the stock page.
9. On an **audio** track, by ear: the hits repitch within the scale. **Unconfirmed on E201:** one
   emulator run kept the stock pitch, a later one in digikit's desktop app on a Mac played EUC+'s
   notes (research log, 2026-11-05). The unit decides.
10. Power off for a few minutes, then on: it boots. The settings are back at their defaults, which is
    expected in v0.2.

Photos, or Elek View screen captures, of steps 3, 4 and 6 help.

## What I need from you

- **Hardware tests** when a build is ready: flash with Elektron Transfer and report back (photos of
  the screen help). **Before the first flash, rehearse recovery** ("Flashing a test build", step 1).
- **Decisions:** ~~ON control~~ (decided: its own on/off control on the page). **Next:** the storage
  location (per pattern or per kit, v0.3).

## Risks

- **OS lock-in:** all addresses are for 1.17. A new Elektron OS means re-finding the sites (the
  method is in the research log).
- Hooks run at interrupt level, so the code stays small and allocation-free.
- Brick risk is low (bootloader untouched, DIN-MIDI recovery), but not zero.
- Since 16 Sep 2026, Elektron does not allow mod files or links on Elektronauts. Share elsewhere,
  as GPL-2.0 source only, never firmware.
