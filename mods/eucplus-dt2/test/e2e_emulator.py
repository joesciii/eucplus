"""End-to-end test of EUC+ in digikit's emulator (paced clock, `--dsp zero`).

Needs a digi-emud started from the stock 1.17 ready snapshot (docs/RESEARCH-LOG.md):
  digi-emud firmware/Digitakt_II_OS1.17.syx --dsp zero \
      --cf-snapshot out/snapshots/dt2-audio-ready-cold-*/digi-audio-m5.snap --run-dir out/native/run --no-audio
then, from the digikit checkout, after tools/hotpatch.py has installed the build:
  uv run python e2e_emulator.py out/native/run MAP.json

It turns track 1 into a MIDI track on channel 1, switches stock Euclidean mode on with PL1 = 5,
sets the pattern to A pentatonic minor, plays, and checks over USB MIDI:
  1. Defaults (EUC+ off on every track): every hit plays the track's NOTE, as stock.
  2. EUC+ switched on for track 1, locked: every note is in the scale, the notes sent equal the notes EUC+ logged,
     and the riff repeats every PLEN passes.
  3. EUC+ on, unlocked: notes stay in the scale and the riff changes.
"""
import json, sys

MS = 132_000
K = {'NO': 12, 'AMP': 4, 'SRC': 2, 'FUNC': 17, 'DOWN': 14, 'YES': 10, 'PLAY': 20, 'STOP': 21, 'PUSH_A': 41}
PAT = 0x41776cb8
A_PENT_MIN = {9, 0, 2, 4, 7}                     # A C D E G


def main():
    run_dir, mapf = sys.argv[1], sys.argv[2]
    sym = {k: int(v, 16) for k, v in json.load(open(mapf)).items() if isinstance(v, str) and v.startswith('0x')}
    cfg, log, seq = sym['eup_cfg'], sym['eup_log'], sym['eup_seq']
    sys.path.insert(0, 'tools/control')
    from digi_client import Emu
    emu = Emu.connect(run_dir=run_dir)

    def run(ms): emu.step(int(ms * MS))
    def tap(k): emu.tap(K[k], hold=20 * MS); run(70)
    def combo(h, k):
        emu.button(K[h], down=True); run(50); emu.tap(K[k], hold=20 * MS); run(70); emu.button(K[h], down=False); run(50)
    def burst(enc, d, n):
        for _ in range(n):
            emu.turn(enc, detents=d); run(2)
        run(100)
    def poke8(addr, val):
        a = addr & ~3; w = emu.peek32(a) or 0; sh = 8 * (3 - (addr & 3))
        emu.poke32(a, (w & ~(0xff << sh)) | ((val & 0xff) << sh))

    def play(ms):
        emu.usb_out(); start = emu.peek32(seq) or 0
        tap('PLAY'); run(ms); tap('STOP'); run(50)
        u = emu.usb_out(); end = emu.peek32(seq) or 0
        notes, i = [], 0
        while i + 2 < len(u):
            if u[i] & 0xF0 == 0x90:
                if u[i + 2]:
                    notes.append(u[i + 1])
                i += 3
            else:
                i += 1
        logged = []
        for j in range(max(start, end - 64), end):
            w1 = emu.peek32(log + 8 * (j % 64) + 4) or 0
            logged.append(w1 >> 24)
        return notes, logged

    tap('NO')
    combo('FUNC', 'SRC')                                  # MACHINE menu: MIDI is the 7th entry
    for _ in range(7):
        tap('DOWN')
    tap('YES'); run(100); tap('SRC'); run(50)
    combo('FUNC', 'PUSH_A')                               # CHAN: enable (channel 1)
    combo('FUNC', 'AMP')                                  # the stock SEQUENCER (Euclidean) menu
    burst(4, 1, 40)                                       # EUC on
    burst(1, 3, 40); burst(1, -1, 36)                     # PL1 up to 16, down to 5
    tap('NO')
    poke8(PAT + 121725, 7)                                # KB SCALE: Pentatonic Minor
    poke8(PAT + 121727, 9)                                # ROOT NOTE: A
    ok = True

    notes, _ = play(4000)                                 # 1. defaults: EUC+ off on every track
    stock = len(set(notes[1:])) == 1
    print('default:  ', notes, '->', 'PASS stock Euclid notes' if stock else 'FAIL')
    ok &= stock

    poke8(cfg + 8, 1)                                     # 2. EUC+ on for track 1 (as v0.2's UI will), locked
    notes, logged = play(10000)
    in_scale = all(n % 12 in A_PENT_MIN for n in notes[1:])
    sent = notes[1:]                                  # the first may be a note queued before PLAY
    same = len(sent) > 5 and any(logged[i:i + len(sent)] == sent for i in range(len(logged) - len(sent) + 1))
    period = 10                                           # 5 hits x PLEN 2
    repeats = len(notes) > period + 2 and notes[1:1 + period] == notes[1 + period:1 + 2 * period][:len(notes[1:1 + period])]
    print('locked:   ', notes)
    print('  logged:  ', logged[-len(notes):])
    print('  in scale %s, notes sent == notes EUC+ chose %s, repeats every 2 passes %s' % (in_scale, same, repeats))
    ok &= in_scale and same and repeats

    t0 = cfg + 8                                          # 3. unlocked, VAR 64
    poke8(t0 + 7, 0); poke8(t0 + 8, 64)
    notes, _ = play(10000)
    in_scale = all(n % 12 in A_PENT_MIN for n in notes[1:])
    changes = notes[1:1 + period] != notes[1 + period:1 + 2 * period]
    print('unlocked: ', notes)
    print('  in scale %s, riff changes %s' % (in_scale, changes))
    ok &= in_scale and changes

    print('RESULT:', 'PASS' if ok else 'FAIL')
    emu.close()
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
