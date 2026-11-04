# Extended Euclid ("EUC+"): research and design

Status: design revision 2. **v0.1 is built and passes in the emulator** (mods/eucplus-dt2). Target: Digitakt II OS 1.17.

Stock file verified: `Digitakt_II_OS1.17.syx`, sha256 `26c22f66…50d5`. The main OS depacks to
3,275,616 bytes, sha256 `a1e7b657…`, both matching Elekloader's `digitakt_mk2` profile. The
firmware is never committed (see `.gitignore`).

Sources: the Digitakt II manual for OS 1.17 (`Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf`,
§10.3, §8.5.2, App. B.6, App. D), strings in the 1.17 image, the OS release notes, and Torso's
T1 docs (docs.torsoelectronics.com, parameter reference and core concepts, read 2026-11-03).

---

## 1. The stock Digitakt II Euclidean mode (verified)

- **Opening it:** **[FUNC] + [AMP]** opens the SEQUENCER menu (manual §10.3). The page name in the
  firmware is `EUCL` / "Euclidean".
- **Parameters:**

  | Param | Meaning | NRPN |
  |---|---|---|
  | PL1 / PL2 | pulses of generator 1 / 2 | 3:8 / 3:9 |
  | LEN | track length (PER TRACK scale mode only) | — |
  | EUC | Euclidean mode on/off | 3:14 |
  | RO1 / RO2 | rotation of generator 1 / 2 | 3:11 / 3:12 |
  | TRO | rotation of the whole track | 3:13 |
  | OP | OR, XOR, AND, SUB (SUB = gen 1 minus gen 2) | 3:10 |
- **Behaviour:**
  - With the mode on, [REC] and the generated trigs light blue.
  - Grid trigs are hidden while the mode is on and come back when it is turned off.
  - You can't add note trigs, but you **can p-lock the generated trigs**.
  - [FUNC] held while turning EUC off converts the generated trigs into grid trigs and replaces the old ones.
  - It works on audio and MIDI tracks; MIDI tracks can use NOT2–4 chords.
- **Code anchor:** the lambdas of `Track::recalculateEuclidean(bool)` (strings at
  `0x401f5d44`). The hits are computed into the track, so at play time they look like ordinary
  trigs. That is why EUC+ can work at the moment a step plays.
- **What the Digitakt II already has that EUC+ can reuse:**
  - **KB SCALE and ROOT NOTE, stored in the pattern** (FUNC + KEYBOARD, §8.5.2), with 36 scales
    (App. D). Verified: one setting per pattern, shared by all tracks, despite the manual's wording. EUC+ follows these by default, so there is no separate scale to set and save.
  - **Retrig on each trig:** RTRG, rate (1/1 … 1/80) and VFAD velocity fade. These are the means for repeats.
  - **Trig PROB, trig conditions, micro timing, NOT2–4.**

What it lacks compared with a T-1: generated pitch, any link to the scale, accent and groove
shapes, repeats on pulses, probability for generated pulses only, and controlled, lockable
randomness.

## 2. Torso T-1: the model we borrow from (verified from Torso's docs)

| Area | T-1 behaviour |
|---|---|
| Rhythm | Steps, Pulses (Euclidean), Rotate, Division |
| **Pitch** | A **note pool** chosen on a keyboard view (only notes in the scale can be chosen). Turning Pitch transposes the pool *within the scale*. Torso's advice: start with 2–4 notes, such as root, 3rd and 5th |
| Harmony | Moves **one chord tone at a time** up or down. It is *hysteretic*: turning back does not undo |
| Scale / Root | Per track, plus a user scale |
| **Range / Phrase** | Range = how much melodic movement. Phrase = its shape: **Cadence 1–4, Saw, Triangle, Sine, Pulse**. Phrase length 1–64 bars (default 4) |
| Voicing / Style | Moves the lowest note up an octave or the highest down. Styles: **Fixed, Ramp, Climb (2 forward, 1 back), Up/Down, Climb Up/Down**, each poly (chord) or mono (arpeggio), with direction up or down |
| Repeats | **Repeats** (count), **Time** (1/16, 1/32 …), **Ramp** (velocity across repeats), **Pace** (repeats speed up or slow down) |
| Velocity / **Prob** | Prob clockwise skips any note including repeats. Counter-clockwise skips **pulses only**; a skipped pulse also skips its repeats |
| Accent / Groove | Bipolar velocity movement around the base velocity, shaped by a groove (Agogo, Timbales, Congas, Bongo, Saw, Tri, Sine…), groove length 1–64 bars |
| **Random** | Each parameter you assign gets its **own 16-step modulation sequence** and **amount** (hold RANDOM, turn the parameter). **Variance** (turn RANDOM) = how much the sequences drift over time. **Lock** per parameter (double-tap) freezes that sequence. There is also slew, phase shift, and *Rate* (clock for "Random Rate" targets; others advance *per cycle*). Clockwise / counter-clockwise give different behaviour per parameter (for example pitch: each chord note differently / all equally). A retrigger resets the sequences |
| Cycles | Stored variations of one track across loop passes |
| Temp | Changes made while holding it revert on release |

The key idea we take: **random is a lockable, drifting 16-step sequence for each parameter, not a
dice roll on every step.** You get a riff you can keep, nudge or release.

## 3. EUC+ design

### Principles
1. **It sits on top of stock Euclid and never replaces it.** EUC+ is **off on every track by default**, so
   stock Euclid keeps its normal note behaviour until you enter EUC+ and switch it on for that track.
   EUC+ only changes the hits as each step plays. With EUC+ off, the track is exactly stock. Pattern data changes only when you press COMMIT.
2. **It is a second layer of the stock SEQUENCER menu.** Entering it does not switch EUC+ on. Each track
   has its own ON control on the NOTES page, and every EUC+ page shows the state.
3. **It works in the Digitakt II's own terms.** Scale and root come from the pattern's KB SCALE / ROOT NOTE
   (one setting per pattern).
   Repeats use the trig's own retrig. Chords use NOT2–4 on MIDI tracks.

### Entry
- **With the stock SEQUENCER (Euclid) menu open, press [FUNC] + [AMP] again** to switch to EUC+.
  Press it once more, or [NO], to go back. It's the same gesture you already use, pressed twice.
- Fallback if that combination is taken: [FUNC] + [RIGHT] inside the menu.
- How it works (v0.2): `ev_draw` recognises the stock page by a signature of its two label rows,
  `ev_key` takes the second [FUNC] + [AMP], EUC+ draws over the page with `ev_draw` and takes the
  encoders through `ev_enc` (research log, "v0.2: the UI"). Key tracking could not work (NO does
  not leave the page) and the title bar shows the last parameter edited. A firmware-state check
  (the top view's vtable and its page number, "Prior art" below) would replace the pixels.
- EUC+ pages go left and right with [LEFT] / [RIGHT].

### Pages (encoders A–H, the stock layout)

**1 · NOTES** (T-1 Pitch / Harmony / Range / Phrase)
| | Param | Values |
|---|---|---|
| A | PTCH | transpose the pool within the scale, ±24 degrees |
| B | RNGE | how far melodic movement reaches, 0–24 degrees |
| C | PHRS | CAD1–4, SAW, TRI, SINE, PULS, RAND |
| D | PLEN | phrase length 1/2/4/8/16/32/64 bars |
| E | HARM | move one pool note up or down a degree (hysteretic, like the T-1) |
| F | SCL | FOLLOW (the pattern's KB SCALE) or one of the 36 scales |
| G | ROOT | FOLLOW or C…B |
| H | ON | EUC+ on/off for this track. Off by default. **Decided: its own control. Opening or closing the EUC+ page never changes it** |

**Note pool:** hold [FUNC] on the NOTES page and the 16 [TRIG] keys become the scale-degree keyboard
(only in-scale notes lit), as on the T-1's Pitch view. The default pool is root, 3rd and 5th.

**2 · VOICE** (T-1 Voicing / Style; full chords on MIDI tracks, mono arpeggio on audio tracks)
| A VOIC amount | B STYL Fixed/Ramp/Climb/UpDn/ClUD | C POLY/MONO | D DIR up/down | E–H reserved |

**3 · GROOVE** (T-1 Velocity / Prob / Accent / Repeats)
| | Param | Notes |
|---|---|---|
| A | PROB | bipolar: + skips any hit, − skips generated pulses only |
| B | ACC | accent depth, bipolar around the trig's VEL |
| C | GRV | accent shape: SAW, TRI, SINE, PULS, EUC3 (a third Euclidean generator), RAND |
| D | GLEN | accent shape length in bars, or the EUC3 pulse count |
| E | REP | repeats 0–8, sent as an RTRG lock on the hit |
| F | RTIM | repeat rate, sent as the retrig rate lock |
| G | RAMP | repeat velocity ramp, sent as the VFAD lock |
| H | PACE | reserved: needs sub-step timing (see open questions) |

**4 · RANDOM** (T-1 Random)
| | Param | Notes |
|---|---|---|
| A | VAR | variance: how much the unlocked sequences drift each pass |
| B | RATE | step clock for Random-Rate targets (1/16 … 4 bars); others advance per cycle |
| C | SEED | re-roll every sequence (as T-1 CLEAR + RANDOM) |
| D–H | — | reserved |

**Assigning random, the T-1 gesture:** on any EUC+ page, **hold [FUNC] and turn a parameter** to set
its random amount. **[FUNC] + press the encoder** toggles LOCK for that parameter. Assigned
parameters show a dot; locked ones show a filled dot.

**COMMIT:** [FUNC] + [YES] on any EUC+ page writes the current result into p-locks (NOTE / NOT2–4,
VEL, RTRG) on the stock Euclid trigs. Combined with stock's FUNC + EUC-off, you can bake it into a
grid sequence.

### The EUC+ screens (v0.2, `ui.c`)

They are drawn in the stock parameter pages' geometry, measured on the stock FLTR and SEQUENCER
pages (rows from the top, 0-63):

| Part | Stock | EUC+ |
|---|---|---|
| Header, rows 0-9 | the pattern box `x 0-14`, a white title bar `x 17-127` with cut corners | the pattern box stays stock; EUC+ draws the bar: `EUC+ Notes (1/2)` on the left in the 6-pixel title font, `T1` on the right. While a knob turns: its long name and value (`Scale: Phrygian`), as stock shows `Pulses Gen 1: 5` |
| Page box, `x 0-14`, rows 13-36 (corners cut) | the machine and track (`SMP` / `1`) | `EUC` in the narrow font, the track number |
| Under it, rows 40-56 | the LEV fader, label `LEV` at 59 | **EUC+'s ON switch**: a box filled when on, `ON` / `OFF` under it. On every page, so it is always plain whether EUC+ drives the track |
| Knob grid | centres `x 33, 59, 85, 111`, rows 21 and 48; radius 8, a pointer from the centre over ±135° (up is the middle); labels at rows 32 and 59 | the same. Numbers are knobs; a list (PHRS, SCL, ROOT) shows its short value where the knob would be; ON and LOCK are toggles (a 17 × 9 rounded box, filled with the knob on the right when on) |
| Turning a knob | the value in the title | the same, and the value inverted in the label's place for 1.5 s (45 ticks), then the label again |

Text is the firmware's: `textf` (`0x40113dc8`) with the stock label font object (5-pixel
capitals) and the narrow font object, centred or right-aligned by its flags; the title through
the plain drawer with the menus' 6-pixel font record. All drawing goes through a canvas (fill and
text callbacks), so `test/ui_host_test.c` renders the same code on the host and checks where things
are. Firmware addresses are in `os117.h`.

Later, two steps closer to stock (not done):
- **B:** blit the firmware's own knob and toggle icons (its icon strips, `0x40114168`), once they are
  found on the Digitakt II (found on the Digitone II, "Prior art" below).
- **C:** make EUC+ a second page of the stock SEQUENCER view (a `ParameterPageView`, as digikit's
  effect pages are): the firmware would draw it, and the second [FUNC] + [AMP] would be its own
  page flip. It needs page and parameter rows whose values live in the kit, so it goes with the
  storage decision (v0.3).

### Engine

```
stock Euclid (recalculateEuclidean) ─► pattern trigs ─► step fire (audio 0x400d6982 / MIDI 0x400d6e88)
   └─ hook at the fire's end (v0.1: 0x400d6e10 / 0x400d7234) ─► eup_fire(event, track, pattern, step, src):
        note   PTCH + pool → PHRS×RNGE → random → scale(KB SCALE, ROOT) → event byte +38   [v0.1, done]
        vel    ACC × GRV → event byte +39                                                [v0.3]
        prob   skip a hit (clear the event)                                              [v0.3, site to find]
        repeat REP/RTIM/RAMP → retrig locks                                              [v0.3, slots to find]
UI: ev_tick/ev_key/ev_enc/ev_draw (core-dt2 1.0) [v0.2, done]     storage: per pattern (pref.) / core_kitdata [v0.3]
```
A general `seq-events-dt2` (the mk1's event API) can wrap these hooks later, for upstream.

- **Random sequences:** for each track and assigned parameter, 16 signed bytes. They are seeded
  from (SEED, track, parameter). At each step boundary (Random Rate or per cycle) an unlocked
  sequence re-rolls each value with chance VAR. A locked one never changes. Pattern restart
  resets the phase, as T-1 retriggers do.
- **Note maths (as built in v0.1):** with `k = (pass mod PLEN)·128 + src` (`src` = the hit's index in
  the pass), `degree = PTCH + pool[k mod |pool|] + phrase(pos)·RNGE/64 + rnd[k]·RAMT/127`, then
  `note = NOTE + ROOT + scale[degree mod n] + 12·(degree div n)`, folded into 36–84 on audio tracks. On audio tracks this
  pitches the sample by semitones. On MIDI tracks it is the real note, with NOT2–4 offsets for VOIC.
- **Storage:** about 24 bytes per track (pool mask 2, 12 params, random amounts and lock bits)
  plus 16 B × assigned sequences. Per-pattern storage is the target, so it follows patterns as
  stock Euclid does. Whether there is room is a phase-1 question. The fallback is a compact
  layout in `core_kitdata` (191 B per kit, shared by all mods), with sequences regenerated from SEED.

### Open questions
1. ~~Do Euclid hits take the grid fire path?~~ **Yes** (verified: same fire functions; the step read is
   the hit's source step).
2. Retrig: the lock slots for RTRG, rate and VFAD, and whether a fire-time lock is honoured (v0.3).
3. ~~Where KB SCALE / ROOT live~~ **Per pattern**, at `+121725` / `+121727` (research log).
4. ~~Whether the SEQUENCER view can be identified, and whether a second [FUNC] + [AMP] is free there~~
   **Yes, free** (identical frames). The page is recognised by its label pixels in `ev_draw`; the view
   object itself was not found (research log, "v0.2: the UI").
5. Free per-pattern bytes for EUC+ state (v0.3).
6. Sub-step timing for PACE and micro-timing offsets on generated repeats (later).
7. ~~The transport state~~ **`0x43164f94`**: 0 stopped, 1 playing, 2 paused. STOP then PLAY restarts the phrase.
8. Writing notes on steps that have p-locks (the lock list at `pattern + 27216`).

### Build order
1. **v0.1 (done, emulator-verified):** the note engine with FOLLOW scale, a 1-3-5 pool, PHRS, RNGE and
   SEED/LOCK/VAR, on audio and MIDI tracks, off by default. The settings are in RAM (`eup_cfg`), with no UI.
2. **v0.2 (done, emulator-verified):** the overlay via a second [FUNC] + [AMP], the NOTES page (with SCL
   and ROOT, FOLLOW by default) and a RANDOM page of VAR, SEED, LOCK, RAMT (RATE later), per-track ON,
   and phrase reset on PLAY. The T-1 random gesture moved to v0.3.
3. **v0.3:** GROOVE (PROB, ACC/GRV, REP via retrig) and storage.
4. **v0.4:** VOICE (chords on MIDI tracks, mono arpeggio), the pool keyboard, HARM, and COMMIT.

## 4. Ecosystem check (altstrument, 2026-11-03)

Digitakt II projects listed:
- **Firmware:** Kári Garðarsson's All-in-One (Plaits, DX7, Déjà Vu LFO; flagged as a brick risk),
  toonst's digitakt2-perform (Elekloader, GPL-2.0), and lalzart's emulator-only dt2-machine-lab.
- **Tools:** DigiSplash-II (boot logo) and lalzart's research notes for 1.15C.

**None of them touch the sequencer.** EUC+ and `seq-events-dt2` would be the first.

## 5. Prior art for UI, structure and tests (survey, 2026-11-03)

Read for v0.2's screens: the Digitakt II / Digitone II mods that draw their own UI, and one
Digitakt mk1 mod for its structure. Addresses below are those projects' findings (most marked
"read once" there); the ones EUC+ uses were checked here and are in the research log.

| Project | Device | What it teaches |
|---|---|---|
| [toonst/digi_macro](https://github.com/toonst/digi_macro) | DT2 1.17, DN2 1.12 (Elekloader, its own core-dt2 1.1 fork) | Screens in the stock geometry (knob grid, page box, MODULATION SETUP layout, list with scroll bar); the stock text routine and font objects; the header message; the view stack to step aside for dialogs; the stock encoder filter; the stock naming screen; a canvas so screens render and are checked on the host, with PNG previews |
| [m-dwyer/digikit](https://github.com/m-dwyer/digikit) findings 50, 61, 64, 71 | DT2 1.17 (DN2 pairs) | A map of the UI framework (view manager, View vtable, drawing primitives, menus, MenuItem, NameView); effect pages that are real stock `ParameterPageView` pages (a page-descriptor row, the page list) |
| [naturarum/dn2-cfw](https://github.com/naturarum/dn2-cfw) T5.0 | DN2 1.12 | Every stock per-parameter glyph is a frame of an icon strip (`std::vector<Bitmap>`), picked by value (17 fonts, 142 strips); each parameter view has a list of page ids (the Euclid page is id 3 on the DN2) |
| [complex-ping/overtone](https://github.com/complex-ping/overtone) | DN2 1.12 (binary patcher) | Finished text-grid pages with small graphics (step bars); no UI source |
| [soejrd/digisophie](https://github.com/soejrd/digisophie) (Digitone port's overlay) | DT1 / DN1 | Gates its overlay on firmware state (brain → screen → shown view), never pixels; redraws only when what it shows changed; yields to stock toasts; per-OS address headers; strict builds |
| [70b185/Aggregat](https://github.com/70b185/Aggregat) | all Elektron boxes (an index, not a mod) | About 50 rules for agents, each from a community incident (`hardware/AGENT-RULES.md`); per-device facts with evidence tags (`hardware/devices/digitakt-2.md`); a feature-overlap list; an open research map. Adopted from it: the ColdFire encoding and no-firmware-bytes build checks (A42, B7), controls for checks (C8), and the flash procedure in PLAN.md (A2-A4, A11, A12, A17, A23, A36, A40, G5, G6) |

**Sequencer prior art** (from Aggregat's feature-overlap list):
- On the Digitakt mk1 and Digitone mk1: alternative sequencers DIGISEQ (a per-track sequencer
  choice on a second TRIG page), DIGISNAKE (after Make Noise René) and TRIG PAGES (TURING).
- Euclid accents in rytm-mods (Analog Rytm).
- Scale handling: four implementations on the Octatrack (36 Digitone II scales in PassOS), 35 scales
  in hugz202's A4 mods, a scale arpeggiator on the Digitone mk1.

On the Digitakt II nothing touches the sequencer yet, so EUC+ would still be the first. DIGISEQ's
second TRIG page is the closest model for tier C above.

**The UI framework (digikit finding 71, DT2 1.17).**
- `ViewController` is at `Brain + 0x40`: a `std::list` of views, sentinel at `+0x14`, last node
  (the top) at `+0x18`, a node's View at `+8`, the redraw byte at `+0x20`.
- digi_macro reads the top view's vtable to tell its base page from FUNC's and TRK's overlays and
  from anything else (a dialog, the naming screen), and then neither draws nor takes keys.
- A `ParameterPageView` keeps its page ids in a vector (`+0x7c` / `+0x80`).
- The page table is RAM `0x4293ba94` (33 rows of `0x2c`: name, title, nine parameter codes), read
  only through `0x400c898a`.
- A page view's vtable slot `+0x88` returns its page number.

  Together these could tell the SEQUENCER page from firmware state instead of pixels.
- **Drawing:**
  - `textf` `0x40113dc8` flags: 1 the proportional path, 2 centre, 4 right-align, 8 invert the box
    after, 0x10 clear the box before (0x18: inverted text in a box, checked here),
    0x20 centre vertically, 0x40 bottom-align.
  - Text width `0x4011467a(font, maxlen, std::string *)`.
  - Scroll bar `0x401143f2`.
  - Invert rect `0x401132c0`.
  - Blit `0x40114168`.
- **The header message:** `UIStates` (`0x401998c0`) and `0x4005b23a(ui, fmt, ...)`. That is how stock
  shows "Filter (1/2)" or "Ptn reloaded"; up to 32 characters, with a timeout.
- **Stock screens and views to reuse:** `NameView` (a text entry, pushed with `0x4011c08a`); `Menu`
  and `MenuItem` (lists, value rows); the encoder filter (stock acceleration, coarse when pushed).
- **Elsewhere:** a core-dt2 1.1 with key LEDs (`ev_key_leds`), frame-time parameter values and
  kit / project storage exists in toonst's Elekloader fork (not upstream on 2026-11-03). Relevant to
  v0.3 storage.

**Methods worth keeping (adopted in v0.2 where marked).**
- Firmware addresses in one header per OS (**adopted:** `os117.h`).
- A canvas between the drawing code and the firmware, so the screens are tested on the host
  (**adopted:** `test/ui_host_test.c`).
- Strict builds that fail loudly (**adopted:** `-Wextra -Werror`, `tools/host_tests.sh`, and
  `build_eucplus.sh` prints the compiler's errors).
- A stock-vs-mod screen comparison: the same key script on the stock and the modded image, with
  frames identical until the mod's own gesture. That proves "off by default" (to do).
- A bit-exact regression check of the engine against a git revision (`git archive` of the old
  `eucplus.c`, the same corpus, compare hashes) before refactoring the note maths (to do).
- A four-character OS version per hardware test build, so device reports name a build
  (**adopted:** `EUP_OS_VERSION`, `E201` for the first; PLAN.md "Hardware builds").
- Redraw only when what the page shows changed; step aside for stock toasts and dialogs (to do,
  with the view-stack check).
