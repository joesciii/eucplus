# Running digikit's emulator natively on a Mac, with EUC+

digikit's own desktop app (the "workbench") runs the Digitakt II OS with the real DSP, in a window
with the front panel, the screen and sound through your Mac's speakers. These steps keep every
tool, cache and build in **one folder, `~/dt2lab`**: delete it and everything is gone.

> **Status:** written from digikit's documentation and from running each step in a Linux cloud
> session (2026-11-05). **Not yet run on a Mac.** If a step fails, send me its last 20 lines.
> Known: on build E201, **audio tracks keep their stock pitch** (EUC+'s notes only reach MIDI
> tracks; being fixed), so the emulator will show that too.

## 0. Once: Apple's command-line tools

The compilers come from Apple's Command Line Tools, a standard system component (many developer tools
need it; it is not part of `~/dt2lab`):

```sh
xcode-select --install          # skip if `xcode-select -p` already prints a path
```

## 1. The folder and its environment

Paste this into a file `~/dt2lab/env.sh`. It points every tool's home, cache and downloads into the
folder:

```sh
export DT2LAB="$HOME/dt2lab"
export PATH="$DT2LAB/bin:$DT2LAB/cargo/bin:$PATH"
export MISE_DATA_DIR="$DT2LAB/mise" MISE_CACHE_DIR="$DT2LAB/cache/mise" MISE_STATE_DIR="$DT2LAB/state/mise"
export MISE_CONFIG_DIR="$DT2LAB/config/mise"
export RUSTUP_HOME="$DT2LAB/rustup" CARGO_HOME="$DT2LAB/cargo"
export UV_CACHE_DIR="$DT2LAB/cache/uv" UV_PYTHON_INSTALL_DIR="$DT2LAB/python" UV_TOOL_DIR="$DT2LAB/uv-tools"
export npm_config_cache="$DT2LAB/cache/npm" npm_config_store_dir="$DT2LAB/cache/pnpm-store"
export PNPM_HOME="$DT2LAB/pnpm" XDG_CACHE_HOME="$DT2LAB/cache"
```

Then, in every Terminal window you use for this:

```sh
mkdir -p ~/dt2lab/bin && source ~/dt2lab/env.sh
```

## 2. The two installers, into the folder

```sh
curl -fsSL https://mise.run | MISE_INSTALL_PATH="$DT2LAB/bin/mise" sh
curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR="$DT2LAB/bin" UV_NO_MODIFY_PATH=1 sh
```

Neither touches your shell's startup files with these settings: the folder is only on your `PATH`
after you `source ~/dt2lab/env.sh`.

## 3. digikit, at the version this project tested

```sh
cd "$DT2LAB"
git clone https://github.com/m-dwyer/digikit.git && cd digikit
git checkout 9a0ddda24962e0bc0cd474f8006173162b4606e4
mise trust && mise install            # Rust, Node, pnpm and friends, into ~/dt2lab
mise run setup                        # its Python and web dependencies
```

## 4. The stock OS 1.17

Download `Digitakt_II_OS1.17_dist.zip` from Elektron's support page, unzip it, and put the `.syx` in
digikit's `firmware/` folder. Check it is the right file:

```sh
cp ~/Downloads/Digitakt_II_OS1.17.syx firmware/
shasum -a 256 firmware/Digitakt_II_OS1.17.syx
# must be 26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5
```

## 5. What the real DSP needs (once, about 30 to 60 minutes)

```sh
# the DSP's register reference (digikit reads its reset values from it)
mkdir -p docs/refs
curl -L -o docs/refs/adsp-2156x-hwr.pdf \
  https://docs.ampnuts.ru/analog.com.datasheet/adsp-21566/related_data/adsp-2156x_hwr.pdf
shasum -a 256 docs/refs/adsp-2156x-hwr.pdf     # e56b2e4e18af9e34c82a9596af377d8d0f3e37ebb02e7b1eae489c7f285930ec
uv run --with pymupdf python -m tools.devtools.refstext docs/refs/adsp-2156x-hwr.pdf --out out/refs

# the shared DSP core, then the emulator's starting state
uv run python -m tools.sharc_dev.sharc_transpile --strict \
  --out out/native/shared-core/gen --work out/native/shared-core/work
export SHARC_GEN_DIR="$PWD/out/native/shared-core/gen"
mise run emu:ready -- dt2 --syx firmware/Digitakt_II_OS1.17.syx

# a +Drive card with a test sample (a 1 kHz sine), and its starting state
uv run python -c "from tools.emulator import dt2_sample_check as d; s, card, ready, w = d.prepare(); print('CARD', card)"
#   note the CARD path it prints

# the fast DSP core for OS 1.17 (without it the DSP runs about 10 times slower)
uv run python -m tools.firmware.extract firmware/Digitakt_II_OS1.17.syx
uv run python -c "from tools.sharc_dev import sharc; sharc.load('dt2-1.17')"
uv run python -m tools.sharc_dev.aot_gen dt2 --card-image <the CARD path>
unset SHARC_GEN_DIR
```

The fast-core steps are the ones that failed for me until I added the two lines before `aot_gen`
(it needs the extracted DSP section and its analysis database).

## 6. Run it with EUC+

Put the test build next to digikit (for E201: `Digitakt_II_OS1.17_EUCPLUS_E201.syx`, SHA-256
`a17115e8…9406`), and samples, if you like, in a folder of `.wav` files:

```sh
mise run workbench -- --no-runtime --plus-drive ~/dt2lab/samples ~/dt2lab/Digitakt_II_OS1.17_EUCPLUS_E201.syx
```

- **`--no-runtime` matters:** without it the workbench patches digikit's own runtime into the OS,
  on top of EUC+. In the app's header, keep **Boot with digikit runtime** unticked too.
- The first launch builds the app (several minutes). Opening a modified OS does a cold boot with the
  DSP, about 15 seconds.
- Then follow the hardware test list in [PLAN.md](../../PLAN.md) ("Hardware builds"). On stock
  parameters the encoders want several ticks per step, as on the unit.

## Removing it

```sh
rm -rf ~/dt2lab
```

That is everything: tools, caches, the OS copy, builds and states. Apple's Command Line Tools stay
(remove them with `sudo rm -rf /Library/Developer/CommandLineTools` if you installed them only for
this).
