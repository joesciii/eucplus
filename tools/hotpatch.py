"""Hot-install an Elekloader build into a running digikit emulator session (paced clock kept).

Writes every byte where the patched main OS differs from stock into guest RAM, then does what
core's boot copier does at power-on: copies the RAM image (__run_load) to __run_start and zeroes
.bss. Use on a session restored from a ready snapshot of the same stock OS (the main screen).

  python3 -I tools/unpack.py ELEKLOADER stock.syx stock.bin
  python3 -I tools/unpack.py ELEKLOADER patched.syx patched.bin
  (from digikit) uv run python hotpatch.py RUN_DIR stock.bin patched.bin patched.syx.map.json
"""
import json, sys

BASE = 0x40000400

def ranges(a, b):
    out, i, n = [], 0, min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and (a[j] != b[j] or (j + 8 < n and a[j:j + 8] != b[j:j + 8])):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    if len(b) > len(a):
        out.append((len(a), len(b)))
    return out

def main():
    run_dir, stock, patched, mapf = sys.argv[1:5]
    a = open(stock, 'rb').read(); b = open(patched, 'rb').read()
    sym = {k: int(v, 16) for k, v in json.load(open(mapf)).items() if isinstance(v, str) and v.startswith('0x')}
    sys.path.insert(0, 'tools/control')
    from digi_client import Emu
    emu = Emu.connect(run_dir=run_dir)
    total = 0
    for lo, hi in ranges(a, b):
        for k in range(lo, hi, 4096):
            chunk = b[k:min(hi, k + 4096)]
            emu.call('mem.cf.write', addr=BASE + k, hex=chunk.hex())
        total += hi - lo
        print('main  %08x..%08x  %d bytes' % (BASE + lo, BASE + hi, hi - lo))
    run_load, run_start, words = sym['__run_load'], sym['__run_start'], sym['__run_words']
    blob = b[run_load - BASE: run_load - BASE + 4 * words]
    for k in range(0, len(blob), 4096):
        emu.call('mem.cf.write', addr=run_start + k, hex=blob[k:k + 4096].hex())
    print('copy  %08x -> %08x  %d bytes' % (run_load, run_start, len(blob)))
    bss, bw = sym['__bss_start'], sym['__bss_words']
    emu.call('mem.cf.write', addr=bss, hex='00' * (4 * bw))
    print('bss   %08x  %d bytes zeroed' % (bss, 4 * bw))
    print('wrote', total, 'main bytes')
    emu.close()

if __name__ == '__main__':
    main()
