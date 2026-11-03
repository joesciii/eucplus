# EUC+ v0.2 (Digitakt II OS 1.17)

T-1-style generated notes on top of the stock Euclidean sequencer, with its own pages on the
device. It is an Elekloader linkable mod (format 2), built on `core` (core-dt2 1.0).

**Status: emulator-tested, not yet run on hardware.** Settings are in RAM and reset at power-on
(storage is v0.3).

## Using it

1. On a track, open the stock Euclidean page with **FUNC + AMP** and switch **EUC** on, as usual.
2. Press **FUNC + AMP again** on that page: the EUC+ pages open (the title bar reads `EUC+ NOTES`).
   Opening them changes nothing: the track keeps playing stock Euclid notes.
3. Turn **H (ON)** to switch EUC+ on for this track. The title bar shows `ON` in a black box while
   EUC+ drives the track, and `OFF` while stock does. Every EUC+ page shows it.
4. Edit with encoders A–H. **LEFT / RIGHT** change page. **NO**, or **FUNC + AMP**, goes back to
   the stock Euclidean page. A page key (SRC, FLTR, AMP, FX, MOD) leaves EUC+ for that page.
   PLAY, STOP, RECORD, TRK + trig (the pages follow the active track), the trig keys and the LEVEL
   encoder keep working while EUC+ is open.

**EUC+ is off on every track by default**, and opening or closing its pages never switches it on
or off. EUC+ never writes pattern data.

### NOTES

| | | Range | Default |
|---|---|---|---|
| A | PTCH | transpose in scale degrees, −24…+24 | 0 |
| B | RNGE | phrase movement in degrees, 0…24 | 4 |
| C | PHRS | phrase shape: RAND SAW TRI SINE PULS CAD1–4 | RAND |
| D | PLEN | phrase length in pattern passes, 1…64 | 2 |
| E | — | (HARM, v0.4) | |
| F | SCL | FLW (the pattern's KB SCALE), CHRM, or one of the 35 KB scales | FLW |
| G | ROOT | FLW (the pattern's ROOT NOTE), or C…B | FLW |
| H | ON | EUC+ on this track | OFF |

### RANDOM

| | | Range | Default |
|---|---|---|---|
| A | VAR | unlocked: the chance a random value re-rolls each pass, 0…127 | 24 |
| B | SEED | the random sequence's seed, 0…255 | 1 |
| C | LOCK | ON keeps the riff exactly | ON |
| D | RAMT | random amount on pitch, in degrees, 0…24 | 3 |

Scale names: IONI DORI PHRY LYDI MIXO AEOL LOCR, PMIN PMAJ (pentatonic), MMIN HMIN (melodic,
harmonic minor), WHOL BLUE CMIN (combo minor) PERS IWAT INSN HIRA PELO PDOM (Phrygian dominant)
WHDM HWDM (diminished) SPAN MLOC SLOC (major, super Locrian) DOB2 (Dorian b2) LAUG LDOM (Lydian
augmented, dominant) DHMJ (double harmonic major) L#26 UPHR HUNM ORNT I#25 LB37 (Locrian bb3 bb7).

## What it does

On a track with both stock Euclidean mode and EUC+ on, each generated hit on a step with no
parameter locks gets a note chosen by EUC+ instead of the track's NOTE:

- **Pool:** scale degrees to pick from, 1-3-5 (the pool keyboard is v0.4).
- **Scale and root:** SCL / ROOT, by default the pattern's own **KB SCALE / ROOT NOTE**
  (FUNC + KEYBOARD). CHROMATIC allows every semitone.
- **Phrase × RANGE:** melodic movement over PLEN pattern passes.
- **Random:** a 16-step random sequence per track, from SEED. LOCK keeps it; unlocked, VAR sets
  how much it changes each pass.
- **Phrase reset:** STOP then PLAY starts the riff (and the random sequence) from its first bar.
  A pause (PLAY while playing) and PLAY again go on where they were.
- **Note range:** audio tracks stay within NOTE 36–84 (the sample is repitched). MIDI tracks
  send the note.
- Steps with p-locks, tracks without Euclid or with EUC+ off, and everything else play as stock.

It hooks the end of the two step-fire functions (audio `0x400d6982`, MIDI `0x400d6e88`) and writes
only the event's note byte (`+38`). The pages use core's `ev_tick`, `ev_draw`, `ev_key` and
`ev_enc`. See [docs/RESEARCH-LOG.md](../../docs/RESEARCH-LOG.md), "v0.2: the UI", for how the
page is recognised, the active track and the transport state.

## Settings (`eup_cfg`, RAM)

`eup_cfg` = `{u32 'EUC+', u8 enable, pad[3], track[16] × 12 bytes}`. Each track entry is
`on, pool, ptch, rnge, phrs, plen, seed, lock, var, ramt, scl, root` ([eucplus.h](eucplus.h)).

## Build and test

From the repo root, in a fresh session:

```sh
bash tools/setup_env.sh        # once per session: toolchain, pinned upstreams, stock OS, emulator (~10 min)
bash tools/build_eucplus.sh    # -> /tmp/digitorsion-work/builds/eucplus.syx (flashable; never commit it)
bash tools/run_e2e.sh          # the emulator test below; must print RESULT: PASS
```

## Tests

- `test/host_test.c`: the note logic on the host
  (`cc -O1 -DEUP_HOST -o t test/host_test.c eucplus.c && ./t`).
- `test/e2e_emulator.py`: digikit's emulator with a paced clock, driven only through keys and
  encoders (the pattern's KB SCALE / ROOT are the only pokes). Track 1 is a MIDI Euclid track
  (PL1 = 5), A pentatonic minor. It checks the USB MIDI notes and the EUC+ screens: stock with the
  defaults; still stock with the EUC+ page open; ON on the page gives in-scale EUC+ notes that
  repeat every PLEN passes and restart from the first bar after STOP / PLAY; PTCH +2 moves the riff
  two degrees; SCL IONI gives A major and ROOT C gives C major; LOCK off with VAR 64 changes the
  riff; ON off gives stock notes again; NO and FUNC + AMP close and reopen it. Screens are saved to
  `/tmp/digitorsion-work/shots`. Result (2026-11-03): `RESULT: PASS`, every check.

The build also cold-boots to the main screen in the emulator.

## Known limits (v0.2)

- No storage: the settings reset at power-on (v0.3).
- Steps with any p-lock keep their stock note.
- The EUC+ page is recognised by the stock page's label pixels. If a future stock dialog covers
  them, FUNC + AMP does the stock thing until it is gone.
- The T-1 random gesture (FUNC + turn) and COMMIT (FUNC + YES) are not there yet.
- Audio-track pitch output is not yet verified by ear. Confirm on hardware.
