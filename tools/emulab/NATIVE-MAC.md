# Running digikit's emulator natively on a Mac, with EUC+

digikit's own desktop app (the "workbench") runs the Digitakt II OS with the real DSP, in a window
with the front panel, the screen and sound through your Mac's speakers. These steps keep every
tool, cache and build in **one folder, `~/dt2lab`**: delete it and everything is gone.

> **Status:** run on a Mac (Apple Silicon, macOS 26.6.2) on 2026-11-05, with digikit `9a0ddda` and
> build E201. About 15 minutes of builds in all, then a 2-minute app build on the first launch.
> Measured: the DSP takes **1.1 CPU seconds per audio second**, close to real time (a few short
> dropouts a minute; `--audio-buffer 1` in step 6 trades them for latency).
> Here, on build E201, the audio test track **does repitch** to EUC+'s notes (measured: research
> log, 2026-11-05), unlike an earlier run in the cloud. With PTCH, RNGE and RAMT at 0 the hits still
> cycle through the pool, 1-3-5 of the scale.

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

mise's installer ends by suggesting a line for `~/.zshrc`: **don't add it**. Neither installer touches
your shell's startup files: the folder is only on your `PATH` after you `source ~/dt2lab/env.sh`.

## 3. digikit, at the version this project tested

```sh
cd "$DT2LAB"
git clone https://github.com/m-dwyer/digikit.git && cd digikit
git checkout 9a0ddda24962e0bc0cd474f8006173162b4606e4
mise trust && mise install            # Rust, Node, pnpm and friends, into ~/dt2lab (about 1 min)
mise run setup                        # its Python and web dependencies
cargo fetch --locked                  # every crate: the app's builds run cargo --offline
rustup target add wasm32-unknown-unknown   # the app's small WASM modules
mise install conda:poppler@26.10.0    # pdftotext, for step 5 (Linux gets it from apt)
```

## 4. The stock OS 1.17

Download it from Elektron, unzip it, and put the `.syx` in digikit's `firmware/` folder. Check it is
the right file:

```sh
curl -sSL -o "$DT2LAB/os117.zip" https://www.elektron.se/wp-content/uploads/2026/10/Digitakt_II_OS1.17_dist.zip
unzip -o -j "$DT2LAB/os117.zip" Digitakt_II_OS1.17.syx -d firmware/
shasum -a 256 firmware/Digitakt_II_OS1.17.syx
# must be 26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5
```

## 5. What the real DSP needs (once, about 10 minutes)

```sh
# the DSP's register reference (digikit reads its reset values from it)
mkdir -p docs/refs
curl -L -o docs/refs/adsp-2156x-hwr.pdf \
  https://docs.ampnuts.ru/analog.com.datasheet/adsp-21566/related_data/adsp-2156x_hwr.pdf
shasum -a 256 docs/refs/adsp-2156x-hwr.pdf     # e56b2e4e18af9e34c82a9596af377d8d0f3e37ebb02e7b1eae489c7f285930ec
PATH="$(mise where conda:poppler@26.10.0)/bin:$PATH" \
  uv run --with pymupdf python -m tools.devtools.refstext docs/refs/adsp-2156x-hwr.pdf --out out/refs

# the shared DSP core (10 s), then the emulator's starting state (about 3 min)
uv run python -m tools.sharc_dev.sharc_transpile --strict \
  --out out/native/shared-core/gen --work out/native/shared-core/work
export SHARC_GEN_DIR="$PWD/out/native/shared-core/gen"
mise run emu:ready -- dt2 --syx firmware/Digitakt_II_OS1.17.syx

# a +Drive card with a test sample (a 1 kHz sine), and its starting state (about 2 min)
uv run python -c "from tools.emulator import dt2_sample_check as d; s, card, ready, w = d.prepare(); print('CARD', card)"
#   note the CARD path it prints

# the fast DSP core for OS 1.17 (about 3 min; without it the DSP runs about 10 times slower)
uv run python -m tools.firmware.extract firmware/Digitakt_II_OS1.17.syx
uv run python -c "from tools.sharc_dev import sharc; sharc.load('dt2-1.17')"
uv run python -m tools.sharc_dev.aot_gen dt2 --card-image <the CARD path>
unset SHARC_GEN_DIR

# the same 1 kHz sine as a +Drive folder for the app: track 1 plays file 1
mkdir -p "$DT2LAB/samples"
cp out/runs/dt2-sample-check/*/samples/test-sine-1khz.wav "$DT2LAB/samples/"
```

Builds print `warning: stripping debug info with rust-objcopy failed ... libLLVM.dylib`: harmless on
macOS (only the debug-info strip is skipped).

## 6. Run it, then load EUC+

Two things that do **not** work, found on the Mac:

- `mise run workbench` first builds digikit's own modules (its runtime), which needs an m68k-elf
  toolchain we don't install and don't want. Start the app directly, as below.
- The app can't start from the EUC+ `.syx`: it identifies the device by the file's SHA-256, does not
  know E201's, and stops ("no device profile recognizes firmware"). So it starts on **stock 1.17**,
  and E201 is loaded into it afterwards, as a cold boot.

Put the test build in `~/dt2lab` (for E201: `Digitakt_II_OS1.17_EUCPLUS_E201.syx`, SHA-256
`a17115e8fc870730fe354a069e2d83728380dd14f41447f466ddf2b8907e9406`). Start the app (leave this Terminal
open; the first launch builds it, about 2 minutes, and prints `control socket: ...` when it is up):

```sh
cd "$DT2LAB/digikit"
mise exec -- uv run python -m tools.emulator.native_emu --no-runtime \
  --plus-drive "$DT2LAB/samples" firmware/Digitakt_II_OS1.17.syx
```

In a second Terminal (`source ~/dt2lab/env.sh` first), load E201 into it (about 11 seconds):

```sh
cd "$DT2LAB/digikit"
mise exec -- uv run python - "$DT2LAB/Digitakt_II_OS1.17_EUCPLUS_E201.syx" <<'PY'
import sys; sys.path.insert(0, 'tools/control')
from digi_client import Emu
from tools.device import devrpc
import time
emu = Emu.connect(run_dir='out/native/run')
r = emu.call('firmware.load', syx=sys.argv[1], start='cold')
print('loaded', r['syx_sha256'][:16], 'runtime:', emu.status()['firmware']['runtime'])
while not emu.status()['cf'].get('main_ui_reached'):
    time.sleep(1)
for _ in range(10):                      # USB answers a few seconds after the main screen
    emu.usb_out(); emu.usb_in(devrpc.build_request(0x02)); time.sleep(1.5)
    out = emu.usb_out(); i = out.find(bytes.fromhex('f000203c10'))
    if i >= 0 and 0xf7 in out[i:]:
        print('version:', devrpc.parse_reply(out[i:out.index(0xf7, i) + 1])[1])   # E201: b'0082\x00E201\x00'
        break
PY
```

- **`--no-runtime` matters:** without it the app patches digikit's own runtime into the OS, on top
  of EUC+. In the app's header, keep **Boot with digikit runtime** unticked too (the script above
  prints `runtime: None` when it is off).
- After the boot the screen shows **FILE SYSTEM OK / PRESS Y/N TO CLOSE**: press NO.
- **Where to see the version:** the unit reports E201 over USB (the script prints it). SETTINGS >
  SYSTEM (USB CONFIG, OS UPGRADE, FORMAT +DRIVE, CHECK FILESYSTEM, STORAGE) does not show it, nor does
  OS UPGRADE's screen.
- **Sound check:** on the main screen TRIG 1 plays track 1, a 1 kHz tone (measured: 1008 Hz, all the
  energy in that bin).
- Then follow the hardware test list in [PLAN.md](../../PLAN.md) ("Hardware builds"). On stock
  parameters the encoders want several ticks per step, as on the unit (EUC's switch about 40).
- On quitting, the app exports the firmware's changes to the card back into `~/dt2lab/samples`.
- Later sessions: only step 6 (the builds and states are cached).

## Removing it

```sh
rm -rf ~/dt2lab
```

That is everything: tools (poppler included), caches, the OS copies, builds and states. Apple's
Command Line Tools stay (remove them with `sudo rm -rf /Library/Developer/CommandLineTools` if you
installed them only for this).
