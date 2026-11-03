# Digitorsion: plan for expanding the Digitakt II's Euclidean sequencer

Status: proposal (2026-11-03). Nothing has been built or flashed yet.

## 1. Where Elektron modding stands (Nov 2026)

| Project | What it is | Digitakt II status |
|---|---|---|
| **Elekloader** (irpina/elekloader, GPL-2.0) | A mod loader. It patches *your* stock `.syx` locally with `.elemod` mods, re-seals the file and verifies it. It never touches the bootloader. | **Experimental, OS 1.17 only.** `core-dt2` 1.0 is a hook bus with `ev_tick`, `ev_draw`, `ev_key`, `ev_enc`, `ev_settings` and `ev_personalize`. It booted on a real unit on 4 Oct 2026 (perform-direct). It has **no sequencer hooks yet.** |
| **Modwerk** (repeat98/modwerk, modwerk.app) | A curated catalogue and builder that sits on top of Elekloader cores | **In research.** Downloads are disabled. A DT2 base was started from toonst's core 1.1 on 30 Oct 2026. Its proposed hooks are `ev_frame_params`, `ev_key_leds`, `core_projdata` and `core_kitdata`. It has no sequencer hooks either. Modwerk's rule is that mods *propose a core hook* and never patch sequencer sites themselves. |
| **digikit** (m-dwyer/digikit) | A runtime for DT2 and DN2 that loads machines and effects from the card, plus a **full emulator** (ColdFire and SHARC) and Ghidra findings | Its runtime runs on a DT2 with OS 1.17. Its emulator is what Elekloader uses to test DT2 builds. It is aimed at audio and DSP, not the sequencer. |
| Mk1 prior art | Elekloader `seq-events-dt1` (step-fire events `ev_gen_trig`, `ev_fire_vel`, `ev_fire_cond`, `ev_fire_notes` and others) and the `euclid` example. Modwerk `euclidean` 1.1.5 is a port of the DT2's stock Euclid UI to the mk1. | Digitakt mk1 only |

Key technical facts for the DT2:
- **ColdFire (m68k) main OS** plus a SHARC DSP. Sequencer logic is on the ColdFire side.
- **Sealed firmware.** The file ends with an HMAC-SHA256 seal. Elekloader derives the key from your own stock file and re-seals. This is solved.
- **Mod RAM:** 256 KB of DDR at `0x47F00000`. Mods are written in C and asm with m68k-elf GCC and binutils.
- The DT2 OS was largely derived from the mk1. Elekloader found `core-dt2`'s sites by matching mk1 code with the addresses masked. This is why porting `seq-events` from the mk1 is realistic.
- **Recovery:** hold FUNC at power-on, press TRIG 4, then send the stock `.syx` **over 5-pin DIN MIDI. USB does not work for recovery.**
- **Community:** since 16 Sep 2026, Elektron does not allow mod files or links on Elektronauts. Discussion moved to elektronmods.com and modwerk.app.

## 2. Chosen route

**Use Elekloader format-2 linkable mods on `core-dt2`, OS 1.17, and test in digikit's emulator before any flash.**

This is done in two layers:

1. **`seq-events-dt2`**, the missing piece. Port Elekloader's `seq-events` device layer from the mk1 (`mods/seq-events-dt1`: 15 sites in the audio and MIDI fire paths) to DT2 1.17. Use the same method `core-dt2` used: masked-opcode matching against the mk1 sites, then confirmation in Ghidra and the emulator. This gives the DT2 the shared `seq-events` API: `ev_gen_trig`, `ev_fire_trig`, `ev_fire_cond`, `ev_fire_vel`, `ev_fire_notes`, `ev_fire_locks`, `sq_clock` and `sq_track_info`. It goes upstream as a PR (Elekloader wants exactly this). Modwerk can later adopt it as a base hook.
2. **`euclid-plus`**, our mod. It is built on `seq-events-dt2` and the core UI events. It **post-processes** what the stock Euclid engine (or the ordinary trigs) produces at fire time, so the stock pattern data and the stock Euclid UI stay untouched.

Why this route and not the alternatives:
- **Not Modwerk-first.** It has no DT2 downloads and no sequencer hooks, and it requires hooks to be proposed upstream anyway. Our work feeds into it later.
- **Not patching the stock Euclid generator directly** (for example, adding a third generator inside Elektron's code). That needs the generator's internals reverse-engineered and an extended pattern format, and stock would drop the extra data on save. It is higher risk and gives worse compatibility. We keep it as an optional phase 5.
- **Not digikit's runtime.** Its strength is DSP machines and effects. We use its emulator and RE notes, not its module system.
- **Fire-time processing is non-destructive.** A project saved with the mod still opens on stock firmware.

## 3. Feature set for `euclid-plus`

**Superseded by [docs/EXTENDED-EUCLID.md](docs/EXTENDED-EUCLID.md):** a T-1-style "EUC+" layer entered from the stock Euclid page. The list below is kept for history.

Tier A (fire-time only, low risk):
1. **Euclid accents / velocity generator:** a third pulse count and rotation that marks accent hits (`ev_fire_vel`).
2. **Per-hit probability and density:** a chance per generated hit (`ev_fire_cond` / `ev_gen_trig`).
3. **Evolving rhythms:** pulse count or rotation that advances every N loops (`sq_clock`). For example, the rotation steps +1 each bar.
4. **Polymetric generator length:** each generator gets its own step length, independent of the track length.
5. **Ratchet / roll generator:** hits on a third Euclid pattern retrigger (if the fire path allows sub-steps; to be verified).
6. **Euclid on MIDI tracks with note/pitch cycling:** an Euclidean melody where the note list advances per hit (`ev_fire_notes`).
7. **Fill-mode alternate generator:** a different pulse count while FILL is held.

Tier B (needs storage and UI):
- Settings saved per pattern or kit through `core_kitdata` / `core_projdata` (core 1.1). There is a **191-byte limit per kit, shared by all mods**, so the settings must be bit-packed (8 tracks × about 6 bytes).
- An extra Euclid page (FUNC plus a key combo) drawn with `ev_draw` and driven by `ev_key` and `ev_enc`.

## 4. Phases

| # | Phase | Output | Exit criterion |
|---|---|---|---|
| 0 | Toolchain | m68k-elf GCC and binutils, Elekloader, digikit emulator, headless Ghidra in this container | `core-dt2` plus `hello-marker-dt2` builds and boots in the emulator |
| 1 | Locate the DT2 fire path | Ghidra project. Map the mk1 `seq-events-dt1` sites to DT2 1.17 addresses, the step-word layout (mk1: trig bit 0x0001, SYN 0x0080…), and where the stock Euclid result enters the fire path | Every site documented with its stock bytes and evidence, as Elekloader's README requires |
| 2 | `seq-events-dt2` | A linkable mod, plus a port of the `euclid` example ("T1 = 5/16") | It passes in the emulator. **First hardware flash (by you):** T1 plays 5/16 and recovery is tested |
| 3 | `euclid-plus` v0.1 | Tier A features 1–3, with parameters hard-coded or set in a SETTINGS row | Emulator plus hardware test checklist |
| 4 | UI and storage | A Euclid+ page, with per-kit or per-pattern settings through core 1.1 data | Projects round-trip on stock and modded firmware |
| 5 (optional) | Deep integration | RE of the stock Euclid generator: native extra parameters on the stock page | Only if phases 1–4 show it is tractable |
| 6 | Upstream | PRs to Elekloader (`seq-events-dt2`, example) and a hook proposal to Modwerk | Merged or reviewed |

## 5. Risks

- **OS lock-in:** everything targets 1.17. A new Elektron OS means re-finding the sites (with the same masked-match method).
- **Timing:** fire handlers run at interrupt level, so the code must be small and allocation-free.
- **Brick risk is low:** the bootloader is untouched and DIN-MIDI recovery is documented. It is still not zero, and warranty implications are yours to weigh.
- **Unknowns:** whether stock Euclid hits pass through the same fire path as ordinary trigs (phase 1 answers this), and whether ratchets are reachable.
