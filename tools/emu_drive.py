"""Drive a running digikit digi-emud (Digitakt II) from a command list.

usage (from a digikit checkout):  uv run python emu_drive.py RUN_DIR CMD [CMD ...]
  load:PATH         cold-boot PATH (.syx) and run to the main UI
  boot              run to the main UI
  step:N            run N instructions (M suffix = millions)
  tap:KEY           press and release a key (name or code)
  combo:HELD+KEY    hold HELD, tap KEY, release HELD (e.g. combo:FUNC+AMP)
  turn:ENC:N        turn encoder A-H (or LEVEL) by N detents
  shot:PATH         save the OLED as a PNG
  log:ADDR:SEQADDR  dump the EUC+ probe ring (ADDR = eup_log, SEQADDR = eup_seq)
  peek:ADDR:N       print N longwords from ADDR
"""
import sys, os, struct

KEYS = {'TRIG': 1, 'SRC': 2, 'FLTR': 3, 'AMP': 4, 'FX': 5, 'MOD': 6, 'PRESET': 7, 'SETTINGS': 8,
        'TEMPO': 9, 'YES': 10, 'UP': 11, 'NO': 12, 'LEFT': 13, 'DOWN': 14, 'RIGHT': 15, 'TRK': 16,
        'FUNC': 17, 'KEYBOARD': 18, 'RECORD': 19, 'PLAY': 20, 'STOP': 21, 'PAGE': 22, 'PTN': 23,
        'SONG': 24, **{'T%d' % (i + 1): 25 + i for i in range(16)}}
ENCS = {**{c: i + 1 for i, c in enumerate('ABCDEFGH')}, 'LEVEL': 9}
MS = 132_000              # ColdFire instructions per emulated ms (digikit pk_harness)
HOLD = 20 * MS           # a short press (the DT2 reads long presses as holds)
GAP = 50 * MS

def num(s):
    s = s.upper()
    if s.endswith('M'):
        return int(float(s[:-1]) * 1_000_000)
    return int(s, 0)

def key(s):
    return KEYS[s.upper()] if s.upper() in KEYS else int(s)

def main():
    sys.path.insert(0, 'tools/control')
    from digi_client import Emu, oled_png
    emu = Emu.connect(run_dir=sys.argv[1])

    def to_ui():
        for _ in range(80):
            if emu.status()['cf'].get('main_ui_reached'):
                break
            emu.step(50_000_000)
        emu.step(150_000_000)

    for cmd in sys.argv[2:]:
        op, _, arg = cmd.partition(':')
        if op == 'load':
            emu.call('firmware.load', syx=os.path.abspath(arg), start='cold')
            to_ui()
        elif op == 'boot':
            to_ui()
        elif op == 'step':
            emu.step(num(arg))
        elif op == 'tap':
            emu.tap(key(arg), hold=HOLD); emu.step(HOLD + GAP)
        elif op == 'combo':
            held, k = arg.split('+')
            emu.button(key(held), down=True); emu.step(GAP)
            emu.tap(key(k), hold=HOLD); emu.step(HOLD + GAP)
            emu.button(key(held), down=False); emu.step(GAP)
        elif op == 'turn':
            enc, n = arg.split(':')
            emu.turn(ENCS[enc.upper()], detents=int(n)); emu.step(GAP)
        elif op == 'burst':                     # burst:ENC:DETENTS:EVENTS, 2 ms apart
            enc, d, n = arg.split(':')
            for _ in range(int(n)):
                emu.turn(ENCS[enc.upper()], detents=int(d)); emu.step(2 * MS)
            emu.step(100 * MS)
        elif op == 'shot':
            open(arg, 'wb').write(emu.frame_png()); print('shot', arg)
        elif op == 'peek':
            a, n = arg.split(':')
            a = num(a)
            print(' '.join('%08x' % (emu.peek32(a + 4 * i) or 0) for i in range(int(n))))
        elif op == 'midi':                      # print the note-ons sent on MIDI DIN since the last call
            data = emu.midi_out()
            names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
            ons, i, status = [], 0, 0
            while i < len(data):
                b = data[i]
                if b & 0x80:
                    status = b; i += 1
                    if b >= 0xF8:
                        continue
                if status & 0xF0 in (0x80, 0x90, 0xA0, 0xB0, 0xE0) and i + 1 < len(data):
                    d1, d2 = data[i], data[i + 1]; i += 2
                    if status & 0xF0 == 0x90 and d2:
                        ons.append('%s%d' % (names[d1 % 12], d1 // 12 - 1))
                elif status & 0xF0 in (0xC0, 0xD0):
                    i += 1
                else:
                    i += 1
            print('midi out: %d bytes, note-ons (MIDI octaves, 60 = C4):' % len(data), ' '.join(ons))
        elif op == 'elog':                      # elog:EUP_LOG:EUP_SEQ (EUC+ v0.1 records, 8 bytes)
            a, s = (num(x) for x in arg.split(':'))
            seq = emu.peek32(s) or 0
            names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
            nn = lambda n: '%s%d' % (names[n % 12], n // 12 - 1)
            print('records:', seq)
            for j in range(max(0, seq - 64), seq):
                base = a + 8 * (j % 64)
                w0 = emu.peek32(base) or 0; w1 = emu.peek32(base + 4) or 0
                t, st, src, nin = w0 >> 24, (w0 >> 16) & 255, (w0 >> 8) & 255, w0 & 255
                nout, deg, ps, fl = w1 >> 24, (w1 >> 16) & 255, (w1 >> 8) & 255, w1 & 255
                print('#%-4d T%-2d pass %3d step %3d src %3d  note %s -> %s  deg %4d  %s' % (
                    j, t + 1, ps, st, src, nn(nin), nn(nout), deg - 256 if deg > 127 else deg,
                    ('euclid ' if fl & 1 else '') + ('LOCKED ' if fl & 2 else '') + ('changed' if fl & 4 else '')))
        elif op == 'log':                       # log:EUP_LOG:EUP_SEQ (probe records, 88 bytes)
            a, s = (num(x) for x in arg.split(':'))
            seq = emu.peek32(s) or 0
            print('records:', seq)
            sw = lambda x: x - (1 << 32) if x & 0x80000000 else x
            for j in range(max(0, seq - 32), seq):
                base = a + 88 * (j % 32)
                w = [sw(emu.peek32(base + 4 * i) or 0) for i in range(22)]
                f = w[21] & 0xffffffff
                b = [struct.unpack('b', bytes([(f >> sh) & 255]))[0] for sh in (24, 16, 8, 0)]
                print('#%d trk %d step %d src %d pat %08x | ev %s | arg %s | euc %d L384 %d L256 %d L512 %d' % (
                    w[0] & 0xffff, w[1], w[2], w[3], w[4] & 0xffffffff,
                    ' '.join(str(x) for x in w[5:13]), ' '.join('%x' % (x & 0xffffffff) for x in w[13:21]),
                    b[0], b[1], b[2], b[3]))
        else:
            raise SystemExit('unknown command ' + cmd)
    emu.close()

if __name__ == '__main__':
    main()
