"""End-to-end test of EUC+ in digikit's emulator (paced clock, `--dsp zero`), through the real UI.

Needs a digi-emud started from the stock 1.17 ready snapshot (docs/RESEARCH-LOG.md):
  digi-emud firmware/Digitakt_II_OS1.17.syx --dsp zero \
      --cf-snapshot out/snapshots/dt2-audio-ready-cold-*/digi-audio-m5.snap --run-dir out/native/run --no-audio
then, from the digikit checkout, after tools/hotpatch.py has installed the build:
  uv run python e2e_emulator.py out/native/run MAP.json [SHOT_DIR]

It turns track 1 into a MIDI track on channel 1, switches stock Euclidean mode on with PL1 = 5,
sets the pattern to A pentatonic minor, and checks over USB MIDI, driving only keys and encoders:
  1. Defaults (EUC+ off on every track): every hit plays the track's NOTE, as stock.
  2. A second FUNC + AMP on the stock Euclidean page opens EUC+ (NOTES), and the notes are still
     stock: opening the page does not switch EUC+ on.
  3. ON (encoder H) on the page: every note is in the scale, the notes sent are the notes EUC+
     logged, the riff repeats every PLEN passes, and it starts from its first bar again after
     STOP and PLAY.
  4. PTCH +2 (encoder A): the same riff, two scale degrees up.
  5. SCL IONI (encoder F): every note is in A major. ROOT C (encoder G): every note in C major.
  6. RANDOM page (RIGHT), LOCK off and VAR 64: notes stay in the scale and the riff changes.
  7. ON off on the page: stock notes again. NO closes EUC+ (the stock page is back), and FUNC + AMP
     on the stock page opens it again, still off.
Each EUC+ screen is checked (the stock pattern box, EUC+'s title bar, the page box and a knob, and
the ON switch filled only when on) and saved as a PNG in SHOT_DIR.
"""
import json, os, sys

MS = 132_000
K = {'NO': 12, 'AMP': 4, 'SRC': 2, 'FUNC': 17, 'DOWN': 14, 'YES': 10, 'PLAY': 20, 'STOP': 21,
     'PUSH_A': 41, 'RIGHT': 15, 'LEFT': 13}
A_PENT_MIN = [0, 3, 5, 7, 10]                       # A C D E G, from A
A_MAJOR = {9, 11, 1, 2, 4, 6, 8}
C_MAJOR = {0, 2, 4, 5, 7, 9, 11}
PAT = 0x41776cb8                                    # the playing pattern
PERIOD = 10                                         # 5 hits x PLEN 2


def deg_shift(n, k, root=9, scale=A_PENT_MIN):
    """The note k scale degrees above note n (n in the scale)."""
    rel = n - root
    octave, semi = divmod(rel, 12)
    d = scale.index(semi) + k
    o2, i = divmod(d, len(scale))
    return root + 12 * (octave + o2) + scale[i]


def main():
    run_dir, mapf = sys.argv[1], sys.argv[2]
    shots = sys.argv[3] if len(sys.argv) > 3 else None
    if shots:
        os.makedirs(shots, exist_ok=True)
    sym = {k: int(v, 16) for k, v in json.load(open(mapf)).items() if isinstance(v, str) and v.startswith('0x')}
    cfg, log, seq, ui = sym['eup_cfg'], sym['eup_log'], sym['eup_seq'], sym['eup_ui']
    sys.path.insert(0, 'tools/control')
    from digi_client import Emu, oled_png
    emu = Emu.connect(run_dir=run_dir)

    def run(ms): emu.step(int(ms * MS))
    def tap(k): emu.tap(K[k], hold=20 * MS); run(70)
    def combo(h, k):
        emu.button(K[h], down=True); run(50); emu.tap(K[k], hold=20 * MS); run(70); emu.button(K[h], down=False); run(50)
    def burst(enc, d, n):
        for _ in range(n):
            emu.turn(enc, detents=d); run(2)
        run(100)
    def turn(enc, d): emu.turn(enc, detents=d); run(100)
    def peek8(addr): return ((emu.peek32(addr & ~3) or 0) >> (8 * (3 - (addr & 3)))) & 0xff
    def poke8(addr, val):
        a = addr & ~3; w = emu.peek32(a) or 0; sh = 8 * (3 - (addr & 3))
        emu.poke32(a, (w & ~(0xff << sh)) | ((val & 0xff) << sh))
    def is_open(): return peek8(ui) == 1
    def t1(field): return peek8(cfg + 8 + field)            # track 1's settings (eucplus.h order)

    def screen(name, on, expect=True):
        """An EUC+ screen in the stock layout: the stock pattern box, EUC+'s title bar (white, with
        dark text), the page box, the knob in slot A, and the ON switch on the left filled only when
        on. Saved as a PNG."""
        run(100)
        _, f = emu.frame()
        px = lambda x, y: (f[(7 - y // 8) + 8 * x] >> (y % 8)) & 1     # y = 0 at the top
        pattern_box = sum(px(x, y) for x in range(15) for y in range(10)) > 40
        bar = all(px(x, 0) and px(x, 9) for x in range(18, 127)) and \
            sum(1 - px(x, y) for x in range(20, 100) for y in range(2, 8)) > 20
        layout = px(7, 13) and px(7, 36) and px(33, 13)                # page box, knob A
        switch = all(px(x, y) for x in range(4, 11) for y in range(42, 55))
        if shots:
            open(os.path.join(shots, name + '.png'), 'wb').write(oled_png(f))
        good = pattern_box and bar and layout and switch == bool(on)
        print('  screen %-15s pattern box %s, title bar %s, layout %s, ON switch %s -> %s' % (
            name, pattern_box, bar, layout, switch, 'PASS' if good == expect else 'FAIL'))
        return good == expect

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

    def check(name, cond):
        print('  %-58s %s' % (name, 'PASS' if cond else 'FAIL'))
        return bool(cond)

    # ---- setup: track 1 a MIDI Euclid track, PL1 = 5, A pentatonic minor ----
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
    poke8(PAT + 121725, 7)                                # KB SCALE: Pentatonic Minor (the test's only
    poke8(PAT + 121727, 9)                                # ROOT NOTE: A                pokes, both stock data)
    ok = True

    print('1. defaults')
    notes, _ = play(4000)
    ok &= check('every hit plays the stock NOTE %s' % notes[:6], len(notes) > 5 and len(set(notes[1:])) == 1)
    stock_note = notes[-1] if notes else None

    print('2. open EUC+ from the stock Euclidean page')
    ok &= check('the stock page is not EUC+ yet', not is_open())
    # the control (a check that cannot fail proves nothing): the screen check refuses the stock page
    ok &= screen('control-stock', on=False, expect=False)
    combo('FUNC', 'AMP')                                  # a second FUNC + AMP: EUC+
    ok &= check('a second FUNC + AMP opens EUC+', is_open())
    ok &= check('opening does not switch it on (ON = %d)' % t1(0), t1(0) == 0)
    ok &= screen('notes-off', on=False)
    notes, _ = play(4000)
    ok &= check('notes still stock with the page open %s' % notes[:6],
                len(notes) > 5 and set(notes[1:]) == {stock_note})

    print('3. ON (encoder H)')
    turn(8, 1)
    ok &= check('ON = 1 for track 1, other tracks off', t1(0) == 1 and peek8(cfg + 8 + 12) == 0)
    ok &= screen('notes-on', on=True)
    notes, logged = play(10000)
    sent = notes[1:]        # the first was built at the last STOP, while EUC+ was still off
    ok &= check('EUC+ notes (not only the stock NOTE)', len(set(sent)) > 2)
    ok &= check('in A pentatonic minor', all((n - 9) % 12 in A_PENT_MIN for n in sent))
    ok &= check('notes sent == notes EUC+ chose',
                len(sent) > 5 and any(logged[i:i + len(sent)] == sent for i in range(len(logged) - len(sent) + 1)))
    ok &= check('the riff repeats every 2 passes',
                len(notes) > 2 * PERIOD + 1 and notes[1:1 + PERIOD] == notes[1 + PERIOD:1 + 2 * PERIOD])
    print('    ', notes)
    again, _ = play(6000)
    ok &= check('after STOP and PLAY it starts from its first bar again',
                len(again) > PERIOD + 1 and again[1:1 + PERIOD] == notes[1:1 + PERIOD])
    # STOP builds the next PLAY's first event (step 0) ahead: EUC+ gives it the riff's first note
    ok &= check('the first note (built at STOP) is the riff\'s first: %s' % again[:1],
                again[:1] == notes[PERIOD:PERIOD + 1])
    riff = notes[1:1 + PERIOD]

    print('4. PTCH +2 (encoder A)')
    turn(1, 2)
    ok &= check('PTCH = +2', t1(2) == 2)
    up, _ = play(6000)
    want = [deg_shift(n, 2) for n in riff]
    print('     ', up[1:1 + PERIOD], 'want', want)
    ok &= check('the same riff two scale degrees up', up[1:1 + PERIOD] == want)
    ok &= screen('notes-ptch', on=True)

    print('5. SCL and ROOT (encoders F, G)')
    turn(6, 2)
    ok &= check('SCL = IONI (2)', t1(10) == 2)
    maj, _ = play(6000)
    ok &= check('in A major (KB ROOT followed) %s' % maj[1:8], len(set(maj[1:])) > 2 and all(n % 12 in A_MAJOR for n in maj[1:]))
    turn(7, 1)
    ok &= check('ROOT = C (1)', t1(11) == 1)
    cmaj, _ = play(6000)
    ok &= check('in C major %s' % cmaj[1:8], len(set(cmaj[1:])) > 2 and all(n % 12 in C_MAJOR for n in cmaj[1:]))
    ok &= check('ROOT changes the notes', cmaj[1:1 + PERIOD] != maj[1:1 + PERIOD])
    ok &= screen('notes-scl', on=True)
    turn(6, -2); turn(7, -1); turn(1, -2)                 # back to FOLLOW, FOLLOW, PTCH 0

    print('6. RANDOM page: LOCK off, VAR 64')
    tap('RIGHT')
    ok &= screen('random', on=True)
    turn(3, -1)                                           # LOCK off
    turn(1, 30); turn(1, 10)                              # VAR 24 -> 64 (a turn is at most 30 detents)
    ok &= check('LOCK = 0, VAR = 64', t1(7) == 0 and t1(8) == 64)
    notes, _ = play(10000)
    ok &= check('in A pentatonic minor, EUC+ notes', len(set(notes[1:])) > 2 and
                all((n - 9) % 12 in A_PENT_MIN for n in notes[1:]))
    ok &= check('the riff changes', notes[1:1 + PERIOD] != notes[1 + PERIOD:1 + 2 * PERIOD])
    print('    ', notes)
    ok &= screen('random-unlocked', on=True)

    print('7. ON off on the page, close, open again')
    tap('LEFT')
    turn(8, -1)
    ok &= check('ON = 0', t1(0) == 0)
    ok &= screen('notes-off-again', on=False)
    notes, _ = play(4000)
    ok &= check('stock notes again %s' % notes[:6], len(notes) > 5 and set(notes[1:]) == {stock_note})
    tap('NO')
    ok &= check('NO closes EUC+', not is_open())
    combo('FUNC', 'AMP')
    ok &= check('FUNC + AMP on the stock page opens it again, still off', is_open() and t1(0) == 0)
    combo('FUNC', 'AMP')
    ok &= check('FUNC + AMP again closes it', not is_open())

    print('RESULT:', 'PASS' if ok else 'FAIL')
    emu.close()
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
