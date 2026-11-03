#!/usr/bin/env bash
# Build EUC+ and link it with core-dt2 into a flashable OS file.
#   bash tools/build_eucplus.sh [WORK_DIR]
# Output: WORK_DIR/builds/eucplus.syx (+ .map.json, and eucplus.main.bin for tools/hotpatch.py).
# The .syx contains Elektron's OS: it stays outside this repository.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(cd "${1:-${DIGITORSION_WORK:-/tmp/digitorsion-work}}" && pwd)"
FW="$WORK/fw/Digitakt_II_OS1.17.syx"
export ELEKLOADER_CROSS="${ELEKLOADER_CROSS:-m68k-linux-gnu-}"
VERSION="$(python3 -c "import json;print(json.load(open('$REPO/mods/eucplus-dt2/mod.json'))['version'])")"
mkdir -p "$WORK/builds"
cd "$WORK/elekloader"
[ -f mods/core-dt2/out/core-1.0.elemod ] || python3 -m elekloader.sdk.build mods/core-dt2 --stock "$FW" | tail -1
LOG="$WORK/builds/eucplus-build.log"
python3 -m elekloader.sdk.build "$REPO/mods/eucplus-dt2" --stock "$FW" > "$LOG" 2>&1 || {
    cat "$LOG"; echo "build_eucplus: the EUC+ build failed (above)" >&2; exit 1; }
tail -1 "$LOG"
python3 -m elekloader.patch --stock "$FW" --mod mods/core-dt2/out/core-1.0.elemod \
    --mod "$REPO/mods/eucplus-dt2/out/eucplus-$VERSION.elemod" --version EUP1 \
    --out "$WORK/builds/eucplus.syx" | tail -2
python3 -I "$REPO/tools/unpack.py" "$WORK/elekloader" "$WORK/builds/eucplus.syx" "$WORK/builds/eucplus.main.bin" | tail -1
python3 -I "$REPO/tools/check_build.py" "$REPO/mods/eucplus-dt2" "$WORK/fw/main_1.17.bin"
