#!/usr/bin/env bash
# EUC+ emulab entrypoint: set up (first run), then run digikit's Digitakt II emulator with
# EUC+ and the browser panel on port 8080.
#
#   emulab start   the real DSP, with sound (the default)
#   emulab fast    a silent stand-in DSP: much faster, the sequencer still runs; MIDI notes shown
#   emulab setup   only the setup (the first run does it by itself)
#   emulab shell   a shell inside the container
#
# /repo is this repository, mounted read-only; it is copied to /work/src for each run, and EUC+ is
# built from that copy. /work is the volume that holds everything else. /samples, when mounted,
# holds .wav files for the emulated +Drive.
set -euo pipefail
W=/work
DT=$W/dt2                       # setup_env.sh's work dir: the OS, elekloader, digikit, the builds
SRC=$W/src
export CARGO_HOME=$W/cache/cargo
mkdir -p "$W/cache" "$W/samples"
say() { printf '\n== emulab: %s\n' "$*"; }

sync_src() {
    rsync -a --delete --exclude .git --exclude 'mods/*/out' /repo/ "$SRC/"
}

default_sample() {              # one plucked tone, C4, when no samples are mounted
    python3 - "$1" <<'PY'
import math, struct, sys, wave
f, sr = 261.63, 48000
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b''.join(struct.pack('<h', int(26000 * math.exp(-4 * i / sr) * sum(
        math.sin(2 * math.pi * f * k * i / sr) / k for k in (1, 2, 3)) / 1.8)) for i in range(sr)))
PY
}

setup() {
    sync_src
    if [ ! -f "$DT/env.sh" ]; then
        say "first setup: the OS from elektron.se, elekloader, digikit, the emulator (about 20 minutes)"
        bash "$SRC/tools/setup_env.sh" "$DT"
    fi
    source "$DT/env.sh"
    cd "$DIGIKIT"
    export SHARC_GEN_DIR="$DIGIKIT/out/native/shared-core/gen"
    if [ -d /samples ] && ls /samples/*.wav >/dev/null 2>&1; then
        rsync -a --delete --include '*.wav' --exclude '*' /samples/ "$W/samples/"
    elif ! ls "$W/samples"/*.wav >/dev/null 2>&1; then
        default_sample "$W/samples/emulab-tone-c4.wav"
    fi
    say "the +Drive card, from $(ls "$W/samples" | wc -l) sample(s)"
    CARD=$(uv run python -c "
import os, pathlib, sys
from tools.emulator import digi_env, dt2_sample_check as d
ne = d.native_emu()
print(ne.plusdrive_card(pathlib.Path('$W/samples'), digi_env.firmware_for('dt2'), digi_env.rust_env(dict(os.environ))))" | tail -1)
    READY=$(uv run python -m tools.emulator.ready dt2 --syx "$STOCK_SYX" --card-image "$CARD" | sed -n 's/^ready_dir=//p')
    if ! uv run python -c "
from tools.emulator import digi_env; import pathlib, sys
gen = digi_env.generated_dir(image=pathlib.Path('$READY/digi-audio-dn2-image.bin'))
sys.exit(0 if gen.resolve() == pathlib.Path('$DIGIKIT/out/native/dt2-audio/gen').resolve() else 1)" 2>/dev/null; then
        say "the fast DSP core for this OS (once: an analysis of the DSP program, then a profile)"
        uv run python -m tools.firmware.extract "$STOCK_SYX" | tail -1
        uv run python -c "from tools.sharc_dev import sharc; sharc.load('dt2-1.17')"
        uv run python -m tools.sharc_dev.aot_gen dt2 --card-image "$CARD"
    fi
    say "the emulator with the real DSP"
    unset SHARC_GEN_DIR
    DAEMON=$(uv run python -m tools.emulator.native_emu --daemon --coupled --build-only \
        --dsp-image "$READY/digi-audio-dn2-image.bin" "$STOCK_SYX" | tail -1)
    say "EUC+, from this repository"
    bash "$SRC/tools/build_eucplus.sh" "$DT" | tail -2
    printf 'CARD=%q\nREADY=%q\nDAEMON=%q\n' "$CARD" "$READY" "$DAEMON" > "$W/emulab.env"
}

wait_socket() {
    for _ in $(seq 1 240); do [ -f "$1/current.json" ] && return 0; sleep 1; done
    echo "emulab: the emulator did not start (see $W/emud.log)" >&2; exit 1
}

run() {
    local mode=$1
    [ -f "$W/emulab.env" ] || setup
    sync_src
    source "$DT/env.sh"; source "$W/emulab.env"
    bash "$SRC/tools/build_eucplus.sh" "$DT" | tail -1          # EUC+ as the repository has it now
    cd "$DIGIKIT"
    RUN=$W/run; rm -rf "$RUN"; mkdir -p "$RUN"
    if [ "$mode" = sound ]; then
        say "starting the emulator with the real DSP"
        "$DAEMON" --auto-coupled --audio-profile-syx "$STOCK_SYX" \
            --dsp-image "$READY/digi-audio-dn2-image.bin" --dsp-state "$READY/digi-audio-m5.snap.dsp" \
            --cf-snapshot "$READY/digi-audio-m5.snap" --card-image "$CARD" --no-audio --run-dir "$RUN" \
            "$STOCK_SYX" > "$W/emud.log" 2>&1 &
        wait_socket "$RUN"
        say "booting EUC+ (a cold boot of the modified OS, with the DSP: a minute or two)"
        uv run python - "$RUN" "$DT/builds/eucplus.syx" <<'PY'
import sys; sys.path.insert(0, 'tools/control')
from digi_client import Emu
emu = Emu.connect(run_dir=sys.argv[1])
emu.call('firmware.load', syx=sys.argv[2], start='cold')
for _ in range(120):
    if emu.status()['cf'].get('main_ui_reached'):
        break
    emu.step(50_000_000)
emu.step(150_000_000)
emu.tap(12, hold=20 * 132000); emu.step(70 * 132000)     # NO: close the boot dialog
print('booted:', emu.status()['firmware'].get('syx_sha256', '')[:16])
PY
    else
        say "starting the emulator with the silent stand-in DSP"
        ./out/native/target/nogen/release/digi-emud "$STOCK_SYX" --dsp zero --cf-snapshot "$READY_SNAP" \
            --run-dir "$RUN" --no-audio > "$W/emud.log" 2>&1 &
        wait_socket "$RUN"
        uv run python "$SRC/tools/hotpatch.py" "$RUN" "$STOCK_MAIN" "$DT/builds/eucplus.main.bin" \
            "$DT/builds/eucplus.syx.map.json" | tail -1
    fi
    say "open http://localhost:8080 on your Mac"
    exec uv run python "$SRC/tools/emulab/panel.py" "$RUN" --port 8080 --pcm "$W/live.f32" \
        --map "$DT/builds/eucplus.syx.map.json"
}

case "${1:-start}" in
    setup) setup ;;
    start) run sound ;;
    fast) run fast ;;
    shell) exec bash ;;
    *) echo "usage: emulab start|fast|setup|shell" >&2; exit 2 ;;
esac
