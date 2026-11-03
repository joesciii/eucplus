# Digitorsion

EUC+: a Torso-T-1-style extension of the Elektron Digitakt II's stock Euclidean sequencer, built as
an Elekloader mod for **OS 1.17**. Start with [PLAN.md](PLAN.md): it has the status, the next tasks
and how to set up a session.

## Layout

- `mods/eucplus-dt2/`: the mod (`eucplus.c` engine, `ui.c` the EUC+ pages, `os117.h` every
  firmware address the C uses, `site.s` hooks, `mod.json`, `test/`).
- `docs/EXTENDED-EUCLID.md`: research (stock Euclid, Torso T-1) and the EUC+ design.
- `docs/RESEARCH-LOG.md`: verified firmware facts (addresses, layouts), and how each was found.
- `tools/`:
  - `setup_env.sh`: rebuild the environment.
  - `build_eucplus.sh`: build and link.
  - `run_e2e.sh`: the emulator test.
  - `host_tests.sh`: the host tests (engine and pages), strict warnings.
  - `emu_drive.py`: drive the emulator by hand.
  - `hotpatch.py`, `unpack.py`, `maskmatch.py`, `sitemap.py`: analysis tools.

## Rules

- **Never commit Elektron firmware** or anything built from it (`.syx`, depacked images,
  disassembly, emulator snapshots). These live in `/tmp/digitorsion-work` (see `.gitignore`).
- **EUC+ must stay off by default** on every track. Stock Euclid keeps its own behaviour until
  the user switches EUC+ on with its own ON control on the EUC+ page (opening the page never switches it on), and EUC+ never rewrites pattern data except by an explicit COMMIT.
- Every change must keep `tools/run_e2e.sh` at `RESULT: PASS` and `tools/host_tests.sh` passing.
- New firmware addresses go in `os117.h` (C) or `site.s` / `mod.json` (sites), nowhere else.
- Record new firmware findings in `docs/RESEARCH-LOG.md`, with their evidence, before relying on them.
- Hook code runs at interrupt level: no firmware calls, no allocation, keep it small.
- Nothing goes to hardware until it passes in the emulator; the user does all flashing.
