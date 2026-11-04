#!/usr/bin/env bash
# Rebuild the Digitorsion working environment in a fresh cloud session (about 10 minutes).
#
#   bash tools/setup_env.sh [WORK_DIR]          (default: /tmp/digitorsion-work)
#
# Gets the toolchain, the pinned Elekloader and digikit checkouts, the stock Digitakt II OS 1.17
# (from elektron.se, checked against its SHA-256), the digikit emulator with its paced-clock ready
# state, builds core-dt2 + EUC+, and writes WORK_DIR/env.sh with every path the other tools need.
# Nothing it downloads or builds goes into this repository.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${1:-/tmp/digitorsion-work}"
mkdir -p "$WORK"
WORK="$(cd "$WORK" && pwd)"

ELEKLOADER_SHA=6910d2aee0173b1249ce10d635c15e2f88b000c7     # irpina/elekloader
DIGIKIT_SHA=9a0ddda24962e0bc0cd474f8006173162b4606e4        # m-dwyer/digikit
FW_URL=https://www.elektron.se/wp-content/uploads/2026/10/Digitakt_II_OS1.17_dist.zip
FW_SHA=26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5
HWR_URL=https://docs.ampnuts.ru/analog.com.datasheet/adsp-21566/related_data/adsp-2156x_hwr.pdf
HWR_SHA=e56b2e4e18af9e34c82a9596af377d8d0f3e37ebb02e7b1eae489c7f285930ec

say() { printf '\n== %s\n' "$*"; }

say "git identity (commits by joesciii, co-authored by Claude in each message)"
git -C "$REPO" config user.name joesciii
git -C "$REPO" config user.email 67607012+joesciii@users.noreply.github.com

say "toolchain (apt)"
if ! command -v m68k-linux-gnu-gcc >/dev/null || ! pkg-config --exists alsa 2>/dev/null; then
    apt-get install -y -q gcc-m68k-linux-gnu binutils-m68k-linux-gnu libasound2-dev pkg-config poppler-utils \
        >/dev/null 2>&1 || { apt-get update -q >/dev/null 2>&1; apt-get install -y -q gcc-m68k-linux-gnu \
        binutils-m68k-linux-gnu libasound2-dev pkg-config poppler-utils >/dev/null; }
fi
m68k-linux-gnu-gcc --version | head -1

checkout() {   # checkout NAME URL SHA
    if [ ! -d "$WORK/$1/.git" ]; then
        git init -q "$WORK/$1"
        git -C "$WORK/$1" remote add origin "$2"
    fi
    if [ "$(git -C "$WORK/$1" rev-parse HEAD 2>/dev/null || true)" != "$3" ]; then
        git -C "$WORK/$1" fetch -q --depth 1 origin "$3"
        git -C "$WORK/$1" checkout -q --detach FETCH_HEAD
    fi
    echo "$1 at $(git -C "$WORK/$1" rev-parse --short HEAD)"
}
say "upstream checkouts (pinned)"
checkout elekloader https://github.com/irpina/elekloader.git "$ELEKLOADER_SHA"
checkout digikit https://github.com/m-dwyer/digikit.git "$DIGIKIT_SHA"

say "stock firmware"
FW="$WORK/fw/Digitakt_II_OS1.17.syx"
if [ ! -f "$FW" ]; then
    mkdir -p "$WORK/fw"
    curl -sSL --max-time 300 -o "$WORK/fw/dist.zip" "$FW_URL"
    (cd "$WORK/fw" && unzip -o -q dist.zip)
fi
echo "$FW_SHA  $FW" | sha256sum -c -
python3 -I "$REPO/tools/unpack.py" "$WORK/elekloader" "$FW" "$WORK/fw/main_1.17.bin"

say "digikit emulator (daemon, ColdFire + paced clock)"
DK="$WORK/digikit"
mkdir -p "$DK/firmware" "$DK/docs/refs"
cp -f "$FW" "$DK/firmware/"
(cd "$DK" && uv sync -q)
(cd "$DK" && uv run python -m tools.emulator.native_emu --daemon --cf-only --build-only \
    firmware/Digitakt_II_OS1.17.syx 2>&1 | tail -1)
if [ ! -f "$DK/docs/refs/adsp-2156x-hwr.pdf" ]; then
    curl -sSL --max-time 300 -o "$DK/docs/refs/adsp-2156x-hwr.pdf" "$HWR_URL"
fi
echo "$HWR_SHA  $DK/docs/refs/adsp-2156x-hwr.pdf" | sha256sum -c -
if [ ! -f "$DK/out/refs/adsp-2156x-hwr/all.txt" ]; then
    (cd "$DK" && uv run --with pymupdf python -m tools.devtools.refstext docs/refs/adsp-2156x-hwr.pdf \
        --out out/refs 2>&1 | tail -1)
fi
if [ ! -d "$DK/out/native/shared-core/gen" ]; then
    (cd "$DK" && uv run python -m tools.sharc_dev.sharc_transpile --strict \
        --out out/native/shared-core/gen --work out/native/shared-core/work 2>&1 | tail -1)
fi
if ! ls "$DK"/out/snapshots/dt2-audio-ready-cold-*/digi-audio-m5.snap >/dev/null 2>&1; then
    (cd "$DK" && SHARC_GEN_DIR="$DK/out/native/shared-core/gen" uv run python -m tools.emulator.ready dt2 \
        --syx firmware/Digitakt_II_OS1.17.syx 2>&1 | tail -2)
fi
SNAP="$(ls "$DK"/out/snapshots/dt2-audio-ready-cold-*/digi-audio-m5.snap | head -1)"

say "build core-dt2 + EUC+"
export ELEKLOADER_CROSS=m68k-linux-gnu-
mkdir -p "$WORK/builds"
(cd "$WORK/elekloader" && python3 -m elekloader.sdk.build mods/core-dt2 --stock "$FW" | tail -1)
bash "$REPO/tools/build_eucplus.sh" "$WORK"

cat > "$WORK/env.sh" <<EOF
# source this: the paths the Digitorsion tools use
export DIGITORSION_REPO="$REPO"
export DIGITORSION_WORK="$WORK"
export ELEKLOADER="$WORK/elekloader"
export DIGIKIT="$DK"
export STOCK_SYX="$FW"
export STOCK_MAIN="$WORK/fw/main_1.17.bin"
export READY_SNAP="$SNAP"
export ELEKLOADER_CROSS=m68k-linux-gnu-
EOF
say "done: source $WORK/env.sh, then tools/run_e2e.sh"
