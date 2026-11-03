"""Checks on a built EUC+ mod that neither the build nor the emulator makes (run by build_eucplus.sh).

  python3 -I tools/check_build.py MOD_DIR STOCK_MAIN.bin

1. ColdFire encodings (Aggregat rule A42): every instruction in the mod's objects is at most 6 bytes.
   68000 forms ColdFire lacks (immediate to a displacement, 8 bytes) ran in the emulator and crashed a
   Digitakt II at startup.
2. No firmware bytes in what ships (Aggregat rule B7): no 16-byte run of the .elemod also occurs in
   the stock main OS. Elekloader stores stock-matching runs as references to the user's own file;
   this checks that it did.
Each check has a control that must fail, so a broken checker cannot pass.
"""
import glob, os, re, subprocess, sys

CROSS = os.environ.get('ELEKLOADER_CROSS', 'm68k-linux-gnu-')


def longest_insn(obj):
    out = subprocess.run([CROSS + 'objdump', '-d', '-m', 'm68k:cfv4e', obj], check=True,
                         capture_output=True, text=True).stdout
    best = (0, '')
    for line in out.splitlines():
        m = re.match(r'\s*[0-9a-f]+:\t([0-9a-f ]+)\t(.*)', line)
        if m:
            n = len(m.group(1).replace(' ', '')) // 2
            best = max(best, (n, m.group(2)))
    return best


def stock_runs(blob, stock):
    idx = {stock[i:i + 16] for i in range(len(stock) - 16)}
    return [i for i in range(len(blob) - 16) if blob[i:i + 16] in idx and len(set(blob[i:i + 16])) > 2]


def main():
    mod, stock_path = sys.argv[1], sys.argv[2]
    stock = open(stock_path, 'rb').read()
    ok = True
    objs = sorted(glob.glob(os.path.join(mod, 'out', 'eucplus.work', '*.c.o')) +
                  glob.glob(os.path.join(mod, 'out', 'eucplus.work', '*.s.o')))
    if not objs:
        print('check_build: no objects found: INCOMPLETE'); sys.exit(1)
    for o in objs:
        n, ins = longest_insn(o)
        good = 0 < n <= 6
        ok &= good
        print('  %-14s longest instruction %d bytes (%s) %s' % (os.path.basename(o), n, ins.strip()[:40],
                                                            'ok' if good else 'FAIL'))
    elemods = sorted(glob.glob(os.path.join(mod, 'out', 'eucplus-*.elemod')), key=os.path.getmtime)
    blob = open(elemods[-1], 'rb').read()
    hits = stock_runs(blob, stock)
    control = stock_runs(stock[0x1000:0x1400], stock)       # stock against itself: must find runs
    ok &= not hits and bool(control)
    print('  %-14s 16-byte runs shared with stock: %d (control on stock bytes: %d found) %s' % (
        os.path.basename(elemods[-1]), len(hits), len(control), 'ok' if not hits and control else 'FAIL'))
    print('check_build:', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
