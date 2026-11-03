# Extended Euclid ("EUC+"): research and design

Status: design draft (2026-11-03). Target: Digitakt II OS 1.17. Stock file verified (main OS depacked: 3,275,616 bytes, sha256 `a1e7b657…`, matching Elekloader):
`Digitakt_II_OS1.17.syx`, sha256 `26c22f66…50d5`. This is the exact file Elekloader's
`digitakt_mk2` profile expects. The firmware itself is never committed (see `.gitignore`).

Marks: **[V]** verified from a source or the firmware. **[U]** unverified (from memory or a
forum post); to be confirmed on your unit or in the firmware.

---

## 1. The stock Digitakt II Euclidean mode

What we know:

- It has been in the OS since 1.00 **[V]** (fixes for it appear in the 1.01, 1.02 and 1.10 release
  notes inside the OS zip). It works on audio and MIDI tracks, and the 1.10 notes mention
  MIDI-track NOT2–4 locks in Euclid sequences **[V]**.
- It is set per track. Its trigs show differently from grid trigs (blue). Turning the mode off
  with **FUNC held** converts ("freezes") the generated hits into ordinary grid trigs **[V, forum + release notes]**.
- **Parameters, read from the OS 1.17 image [V]:** `PL1` Pulse Gen 1, `PL2` Pulse Gen 2,
  `EUC` Euclidean Mode (on/off), `RO1` Rotation Gen 1, `RO2` Rotation Gen 2, Track Rotation,
  Bool Operator (OR / XOR / AND / SUB; `XOR` and `SUB` sit in the value list beside the
  retrig rates) and Track Length. The page is named `EUCL` / "Euclidean". In the page list it
  sits right after `RTRG` "Retrig Parameters" (strings at `0x40242fc4`–`0x4024302c` and `0x402415fd`).
- **Code anchor [V]:** the C++ lambdas of `Track::recalculateEuclidean(bool)` (strings at
  `0x401f5d44`). This is where the hits are generated, and phase 1 starts here.
- Modwerk's mk1 "Euclidean" module copies this layout from the DT2.
- Parameter locks, conditions and swing apply to the generated hits **[V, release notes]**.

What it lacks compared with a T-1:

| Missing | Effect |
|---|---|
| Pitch generation | Every hit plays the same NOTE unless you p-lock it by hand |
| Scale or key | The Digitakt keeps no scale (`sq_track_info` reports `SQ_SCALE_NONE`) |
| Repeats / ratchets per pulse | None in Euclid mode (grid trigs have RTRG) |
| Accent pattern | All hits play at one velocity |
| Probability per generated hit | Only via trig conditions you set by hand |
| Controlled randomness (seed, lock, variance) | None |
| Phrase or melodic motion | None |

## 2. Torso T-1: the functions that matter here

Per track, as described by Torso's docs and manuals (all **[V]** from search summaries;
the docs site was not reachable from this session):

- **Rhythm:** STEPS, PULSES (Euclidean spread), ROTATE, DIVISION (clock rate).
- **Repeats:** REPEATS (extra notes after each pulse), TIME (their spacing), PACE (the spacing
  speeds up or slows down), with a velocity ramp across the repeats.
- **Tonal:** PITCH (picks the note pool and transposes it within the scale), SCALE and ROOT per
  track, HARMONY (changes the intervals between the selected notes), VOICING (chord size),
  RANGE (how far melodic movement reaches across the pool), PHRASE (the shape of that movement,
  from cadence-like to continuous). Torso's own advice: a small note pool makes stronger melodies.
- **Groove:** VELOCITY, PROBABILITY (clockwise: all notes including repeats; counter-clockwise:
  pulses only), ACCENT, SUSTAIN (gate), TIMING (swing / micro-timing), track DELAY, GROOVE
  templates.
- **Random:** hold RANDOM and turn a parameter to set that parameter's random amount; turning
  RANDOM alone sets *variance*. Each parameter has its own modulation sequence. Since OS 2.1 it
  can be **locked** per parameter (double-tap), which freezes that random sequence while other
  parameters keep varying.
- **Temp:** hold to make temporary changes that snap back on release.
- **Cycles:** pattern-level variation that rescales random amounts.

The key idea to borrow: **randomness comes from a sequence, not a dice roll on every step.**
A seeded sequence repeats until you let it vary or re-roll it, so a "random" melody becomes a
riff you can keep.

## 3. EUC+ design

### Principles
1. **It never replaces stock Euclid.** EUC+ only post-processes the hits the stock generators
   produce, at the moment each step plays. With EUC+ off, a track behaves exactly as stock does.
   Stock pattern data is never rewritten, except by an explicit "freeze" that you trigger.
2. **It is entered from the stock Euclid page.** It is a second layer of that page, not a new mode.
3. **Its randomness is deterministic.** Every random choice comes from `hash(seed, track, hit
   index, cycle)`. While **LOCK** is on, the cycle is fixed at 0, so the same riff repeats forever. While it is
   off, the cycle advances each loop by an amount set by VAR.

### Entry
While the stock Euclidean page is open (detected through `core_view_top`, core 1.1), a key
combination opens **EUC+** as an overlay. EUC+ draws through `ev_draw` and takes the encoders and
keys through `ev_enc` / `ev_key`. NO, or the same combination again, returns to the stock page.

Candidate combinations, to be tested on your unit for conflicts: **FUNC + YES** or **press the
page key that opened Euclid a second time**. The final choice comes after phase 1 maps which keys
the Euclid view already uses.

### Pages (8 encoders, A–H, as on stock pages)

**EUC+ 1 / TONAL**
| Enc | Param | Range | Notes |
|---|---|---|---|
| A | SCL scale | CHR, MAJ, MIN, DOR, PHR, LYD, MIX, LOC, HMIN, MMIN, PMAJ, PMIN, BLUE, WHOLE, USER | USER = a 12-key mask |
| B | ROOT | C…B | |
| C | PTCH pitch | -24…+24 scale degrees | base degree; transposes inside the scale |
| D | RNGE range | 0…24 degrees | spread of melodic motion; 0 = every hit plays PTCH |
| E | PHRS phrase | RAND, UP, DOWN, U/D, CONV, DIVG, WALK, CAD | the shape that RNGE follows |
| F | POOL | 1…7 | how many scale degrees are allowed (the "small pool") |
| G | VOIC voicing | 1…4 | MIDI tracks: chord notes stacked from the scale. Audio tracks: 1 |
| H | HARM harmony | 0…n | interval spread of the voicing (3rds → 4ths → 5ths …) |

**EUC+ 2 / GROOVE**
| Enc | Param | Notes |
|---|---|---|
| A | ACC accent pulses | a third Euclidean generator over the same length; its hits are accented |
| B | ACRO accent rotate | |
| C | ACCV accent amount | velocity added on accented hits |
| D | PROB | +: every hit; −: only non-accented hits (a variation of the T-1's ±) |
| E | REP repeats | 0…8, implemented as the trig's own retrig (RTRG lock at fire time) **[to verify]** |
| F | RTIM repeat time | maps to the retrig rate |
| G | RVEL repeat ramp | maps to the retrig velocity curve |
| H | VELR velocity random | |

**EUC+ 3 / RANDOM**
| Enc | Param | Notes |
|---|---|---|
| A | SEED | 0…255. A new seed gives a new riff |
| B | LOCK | ON = the riff repeats exactly; OFF = it evolves |
| C | VAR variance | while unlocked: the chance a hit re-rolls each cycle (0 = never, 127 = always) |
| D | LOOP | random-sequence length in hits (1…64), independent of the track: a polymeter |
| E | EVO evolve | every N loops: RO1 +1, or PTCH +1 degree (pick one) |
| F | FILL | while FILL is held: an alternative PL1 |
| G | — | reserved |
| H | FRZE freeze | press to write the current EUC+ result into p-locks (NOTE, VEL, RTRG) on the stock Euclid hits |

Micro-timing / TIMING and SUSTAIN on audio tracks are left out of v1. Whether they can be
changed at fire time is unknown (phase 1). SUSTAIN on MIDI tracks maps to the note length lock.

### How it plugs into the firmware

```
stock Euclid generators ──► step fire ──► seq-events-dt2 ──► EUC+ handlers ──► voice / MIDI out
                                            ev_gen_trig   (PROB, FILL)
                                            ev_fire_notes (SCL/ROOT/PTCH/RNGE/PHRS/VOIC)
                                            ev_fire_vel   (ACC, VELR)
                                            ev_fire_locks (REP → RTRG/RTIM/RVEL)
UI: core_view_top + ev_key/ev_enc/ev_draw ─► EUC+ pages        storage: core_kitdata / projdata
```

- **Notes on audio tracks** change the sample's pitch in semitones, the same as a NOTE lock.
  Scale-quantised melodies work on any pitched sample. **On MIDI tracks** up to 4 notes per step
  are sent, so VOIC and HARM give chords. That is the closest to a T-1 driving a synth.
- **Note maths:** `degree = PTCH + phrase(hit, RNGE) mod POOL`, then `note = ROOT + scale[degree]
  + 12·octave(degree)`, added to the track's NOTE.
- **Storage:** about 12 bytes per track × 16 tracks (bit-packed). The kit-data room is 191 bytes
  per kit, shared by all mods (core 1.1). That is too tight to share. The preferred home is
  **per-pattern data**, like stock Euclid; whether a free area exists there is a phase-1 question.
  The fallback is kit data, with a compact 8-byte-per-track layout.

### Prerequisites (from PLAN.md)
1. `seq-events-dt2`: the step-fire event layer, ported from the mk1. Nothing here works without it.
2. Core 1.1 (toonst) for `core_view_top` and the data tables.
3. Phase-1 RE answers: where Euclid hits enter the fire path, whether generated hits and grid trigs
   share it, the retrig lock slots, the Euclid view's identity and unused key combinations,
   and per-pattern storage.

### Build order
1. **v0.1 (TONAL only, fixed UI):** scale, root, pitch, range and RAND phrase with seed and lock,
   set from a SETTINGS row. This proves `ev_fire_notes` on DT2 hardware.
2. **v0.2:** the EUC+ overlay entered from the stock Euclid page, and pages 1–3 without storage.
3. **v0.3:** storage, accent, prob, repeats, evolve and fill.
4. **v0.4:** freeze into p-locks; the remaining phrases; MIDI voicing and harmony.
