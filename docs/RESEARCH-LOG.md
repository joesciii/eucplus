# Research log

Facts gathered while working, with how each was obtained, so that work can resume in a fresh
session. **[V]** = verified here. **[D]** = deduced from disassembly, not yet run.

## Firmware files (never committed)

| File | sha256 | Notes |
|---|---|---|
| `Digitakt_II_OS1.17.syx` (from the user's upload; also at elektron.se `wp-content/uploads/2026/10/Digitakt_II_OS1.17_dist.zip`) | `26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5` | Matches Elekloader `devices/digitakt_mk2.py` **[V]** |
| DT2 main OS (section 3, depacked) | `a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964` | 3,275,616 bytes, loads at `0x40000400`, device byte `0x14` **[V]** |
| `Digitakt_OS1.54.syx` (mk1, elektron.se `…/2026/10/Digitakt_OS1.54_dist.zip`) | `f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6` | Matches Elekloader `digitakt_mk1.py` **[V]** |
| mk1 1.54 main OS | `5c58bf9e3949ef09977c5fc007a61e8d026931f67f1621238379dfb8ee4d31a2` | 2,479,680 bytes **[V]** |
| DT2 manual OS 1.17 | — | elektron.se `…/2026/10/Digitakt-2-User-Manual_ENG_OS1.17_260930.pdf` |

## Reproducing the analysis

```sh
git clone --depth 1 https://github.com/irpina/elekloader
python3 -I tools/unpack.py elekloader Digitakt_II_OS1.17.syx main_1.17.bin    # depack section 3
m68k-linux-gnu-objdump -D -b binary -m m68k:cfv4e --adjust-vma=0x40000400 main_1.17.bin > main.dis
python3 -I tools/maskmatch.py mk1.dis dt2.dis 4006f4be 4006fa40 6        # where an mk1 function sits in the DT2
python3 -I tools/sitemap.py mk1.dis dt2.dis 400d6000 400d7800 <mk1 sites> # per-site alignment
```

Toolchain: Ubuntu `gcc-m68k-linux-gnu` / `binutils-m68k-linux-gnu` 13.3 (apt). For Elekloader set
`ELEKLOADER_CROSS=m68k-linux-gnu-`.

## Environment notes (cloud session)

- The WebFetch tool cannot resolve some hosts even with full network access. `curl` through
  the proxy works.
- The permission check allowed running Elekloader's unpacker after the user's go-ahead. It
  **blocked** `uv sync` in digikit ("code from external"), which the emulator needs. It needs an
  explicit permission rule.

## The Digitakt II step fire (OS 1.17) [D]

Found by masked-opcode matching of the mk1's fire (`0x4006f4be`, the seq-events-dt1 sites) against
the DT2. 76 of 446 six-instruction shingles fall in `0x400d6800`–`0x400d6c00`.

| | mk1 1.54 | DT2 1.17 |
|---|---|---|
| audio fire, entry | `0x4006f4be` | **`0x400d6982`** (`lea -52(sp)`, `movem d2-d7/a2-a6`) |
| MIDI fire, entry | `0x4006f846` | **`0x400d6e88`** (same shape) |
| direct callers | — | none: reached through a pointer (to find) |
| track record stride | 911 (`d3 = 911·track`) | **1701** (`mulsl #1701`) |
| args after prologue | step `76(sp)`, event `80(sp)` | track `56(sp)`, pattern `60(sp)`, step `68(sp)`, event `72(sp)` |
| step word | `pattern + 911·t + 2·step` | `pattern + 1701·t + 2·step` (`a5 = 1701·t`) |
| trig bit / skip bit | `0x0001` / `0x0004` | the same: `0x0001` / `0x0004` |
| SYN default bit | `0x0080`, override `0x0800` | the same: `bset #7` when `+1154 & 0x80` and step bit 11 is clear |
| per-step cond bytes | — | `track + step + 640 / 768 / 896` (`896 == -1` means none) |
| condition routine | — | `jsr 0x401385e4` (6 args: track, step, c640, c768, c896, has-event) |
| per-track PROB | `908(a0)` | **`1168`** → `jsr 0x4013859c` |
| per-track byte `+1444` | — | if nonzero: `step = step mod it`, then `step = word at +2·(722+step)`. Probably a scale or length map; to check |
| **Euclid flag** | — | **`+1176`**: `seq d6` when it is 1 |
| **Euclid step map** | — | when the flag is on: `src = word at 1701·t + 2·(594+step)`. If negative, there is no hit (the event is cleared). Otherwise the fire reads step word `src`, ORs in the trig bit, keeps bit `0x10` from the grid word and sets bit 16 |
| global bytes | — | `121721` / `121722` in the pattern: a mode select used with the table at `0x4020e28c` |

What this means:
- **Stock Euclid hits pass through the same fire function as grid trigs.** The generator's result
  is a per-track map (`+1188`, 2 bytes per step) from play step to source step. EUC+ can hook here,
  as `seq-events` does on the mk1, and can tell a Euclid hit from a grid trig (the flag and the map).
- `Track::recalculateEuclidean(bool)` (lambda name strings at `0x401f5d44`) should be what writes
  the map at `+1188`. Find it next, to read PL1/PL2/RO/TRO/OP and LEN from the track record.
- The mk1 sites do not map one to one: the DT2's trig test is `tst.w` on masked words, not the
  mk1's `move.w … and.l`. The `seq-events-dt2` site list has to be made from this code, not copied.

## Still to find

1. The fire's caller (pointer table) and the note site (the mk1's `lea (a3,d3.l),a0; moveq #-1`).
2. The velocity, lock and note-count sites after `0x400d6b00`.
3. KB SCALE / ROOT in the track or pattern record, and the firmware's scale tables.
4. The retrig lock slots.
5. The SEQUENCER (Euclid) view object, for `core_view_top`.
