#!/usr/bin/env bash
# Run the EUC+ end-to-end emulator test: start a paced-clock digi-emud from the stock ready
# snapshot, hot-install WORK_DIR/builds/eucplus.syx, run mods/eucplus-dt2/test/e2e_emulator.py,
# stop the daemon. Needs tools/setup_env.sh to have run.
#   bash tools/run_e2e.sh [WORK_DIR]
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(cd "${1:-${DIGITORSION_WORK:-/tmp/digitorsion-work}}" && pwd)"
source "$WORK/env.sh"
cd "$DIGIKIT"
RUN=out/native/run
rm -f "$RUN/current.json"
./out/native/target/nogen/release/digi-emud firmware/Digitakt_II_OS1.17.syx --dsp zero \
    --cf-snapshot "$READY_SNAP" --run-dir "$RUN" --no-audio > "$WORK/emud.log" 2>&1 &
DAEMON=$!
trap 'uv run python -m tools.control.digi_client quit >/dev/null 2>&1 || kill $DAEMON 2>/dev/null || true' EXIT
for _ in $(seq 1 120); do [ -f "$RUN/current.json" ] && break; sleep 1; done
uv run python "$REPO/tools/hotpatch.py" "$RUN" "$STOCK_MAIN" "$WORK/builds/eucplus.main.bin" \
    "$WORK/builds/eucplus.syx.map.json" 2>&1 | grep -v UV_NATIVE | tail -1
uv run python "$REPO/mods/eucplus-dt2/test/e2e_emulator.py" "$RUN" "$WORK/builds/eucplus.syx.map.json" \
    2>&1 | grep -v UV_NATIVE
