"""A browser front panel for a running digikit emulator (digi-emud): screen, keys, encoders, LEDs,
USB MIDI notes and the DSP's audio, served over HTTP so the emulator can run in a container.

  (from a digikit checkout) uv run python panel.py RUN_DIR [--port 8080] [--pcm FILE]

The emulator must be a coupled build (real DSP) for sound. The panel sets it running ("free" mode)
and taps its audio into FILE (raw float32 stereo, 48 kHz), which the page streams and plays.
Only the standard library and digikit's tools/control/digi_client.py are used.
"""
import argparse, base64, json, os, pathlib, sys, threading, time, tomllib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, 'tools/control')
from digi_client import Emu  # noqa: E402

KEYS = {'TRIG': 1, 'SRC': 2, 'FLTR': 3, 'AMP': 4, 'FX': 5, 'MOD': 6, 'PRESET': 7, 'SETTINGS': 8,
        'TEMPO': 9, 'YES': 10, 'UP': 11, 'NO': 12, 'LEFT': 13, 'DOWN': 14, 'RIGHT': 15, 'TRK': 16,
        'FUNC': 17, 'KEYBOARD': 18, 'RECORD': 19, 'PLAY': 20, 'STOP': 21, 'PAGE': 22, 'PTN': 23,
        'SONG': 24, **{'T%d' % (i + 1): 25 + i for i in range(16)}}
PCM_LIMIT = 64 << 20          # restart the capture file past 64 MB (about 3 minutes of audio)
RING_BYTES = 48000 * 8 * 120  # the last 2 minutes of audio, for late or repeated reads


class Panel:
    def __init__(self, run_dir, pcm_path, map_path=None):
        self.emu = Emu.connect(run_dir=run_dir)
        self.eup = None           # EUC+'s note log (eucplus.c's eup_log ring), when its map is given
        if map_path and os.path.exists(map_path):
            sym = {k: int(v, 16) for k, v in json.load(open(map_path)).items()
                   if isinstance(v, str) and v.startswith('0x')}
            if 'eup_log' in sym and 'eup_seq' in sym:
                self.eup = (sym['eup_log'], sym['eup_seq'])
        self.eup_seen = None
        self.eup_notes = []       # (track, step, note in, note out, changed) of EUC+'s last hits
        self.lock = threading.Lock()
        self.pcm_path = pcm_path
        self.ring = bytearray()
        self.ring_start = 0       # the absolute byte position of ring[0]
        self.file_pos = 0
        self.audio = False
        self.midi = []            # (time, note, velocity) of the last USB MIDI note-ons
        self.led_of = self._led_map()
        with self.lock:
            try:
                self.emu.call('pcm.tap', action='capture', path=pcm_path)
                self.audio = True
            except Exception as e:  # a ColdFire-only build has no DSP audio
                print('panel: no audio (%s)' % e, file=sys.stderr)
            self.emu.call('run.mode', mode='free')
        threading.Thread(target=self._pump, daemon=True).start()

    @staticmethod
    def _led_map():
        path = pathlib.Path('devices/digitakt-ii-leds.toml')
        try:
            keys = tomllib.loads(path.read_text())['key']
        except (OSError, KeyError, tomllib.TOMLDecodeError):
            return {}
        return {k['code']: k['led'] for k in keys if isinstance(k.get('led'), int) and k['led'] >= 0}

    def _pump(self):
        """Read new audio from the capture file into the ring; collect USB MIDI note-ons."""
        while True:
            time.sleep(0.05)
            if self.audio:
                try:
                    with open(self.pcm_path, 'rb') as f:
                        f.seek(self.file_pos)
                        data = f.read()
                    data = data[:len(data) - len(data) % 8]
                    if data:
                        self.file_pos += len(data)
                        self.ring += data
                        over = len(self.ring) - RING_BYTES
                        if over > 0:
                            over -= over % 8
                            del self.ring[:over]
                            self.ring_start += over
                    if self.file_pos > PCM_LIMIT:
                        with self.lock:
                            self.emu.call('pcm.tap', action='capture', path=self.pcm_path)
                        self.file_pos = 0
                except OSError:
                    pass
            try:
                with self.lock:
                    u = self.emu.usb_out()
            except Exception:
                continue
            i, now = 0, time.time()
            while i + 2 < len(u):
                if u[i] & 0xF0 == 0x90:
                    if u[i + 2]:
                        self.midi.append((now, u[i + 1], u[i + 2]))
                    i += 3
                else:
                    i += 1
            del self.midi[:-32]

    def audio_from(self, pos):
        end = self.ring_start + len(self.ring)
        pos = max(pos, self.ring_start, end - 48000 * 8 * 2) if pos >= 0 else end
        pos -= (pos - self.ring_start) % 8
        chunk = bytes(self.ring[pos - self.ring_start:pos - self.ring_start + 48000 * 8])
        return pos, pos + len(chunk), chunk

    def _eup_log(self):
        """New records of EUC+'s ring (8 bytes each: track, step, src, note in / note out, degree,
        pass, flags; flags 4 = EUC+ chose the note). Called with the lock held."""
        log, seq = self.eup
        n = self.emu.peek32(seq) or 0
        if self.eup_seen is None or n < self.eup_seen:
            self.eup_seen = max(0, n - 8)
        for j in range(max(self.eup_seen, n - 16), n):
            w0 = self.emu.peek32(log + 8 * (j % 64)) or 0
            w1 = self.emu.peek32(log + 8 * (j % 64) + 4) or 0
            self.eup_notes.append((w0 >> 24, (w0 >> 16) & 255, w0 & 255, w1 >> 24, bool(w1 & 4)))
        self.eup_seen = n
        del self.eup_notes[:-12]

    def state(self):
        with self.lock:
            rev, frame = self.emu.frame()
            leds = self.emu.leds().get('leds') or ''
            st = self.emu.status()
            if self.eup:
                self._eup_log()
        return {'rev': rev, 'frame': base64.b64encode(frame).decode(), 'leds': leds,
                'icount': st.get('icount'), 'elapsed': st.get('elapsed'), 'mode': st.get('mode'),
                'audio': self.audio, 'midi': self.midi[-12:], 'eup': self.eup_notes if self.eup else None}

    def button(self, code, down):
        with self.lock:
            self.emu.button(int(code), down=bool(down))

    def turn(self, enc, d, repeat=1):
        """d detents, repeat times, 20 ms apart: stock parameters turn through the firmware's
        encoder filter, which wants several separate ticks per step, as from a real encoder."""
        for i in range(max(1, min(40, int(repeat)))):
            if i:
                time.sleep(0.02)
            with self.lock:
                self.emu.turn(int(enc), detents=max(-30, min(30, int(d))))


def serve(panel, port):
    page = (HERE / 'panel.html').read_bytes()

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, body, ctype='application/json', extra=()):
            self.send_response(200)
            self.send_header('Content-Type', ctype)
            self.send_header('Cache-Control', 'no-store')
            for k, v in extra:
                self.send_header(k, v)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path, _, query = self.path.partition('?')
            q = dict(p.split('=', 1) for p in query.split('&') if '=' in p)
            if path == '/':
                self._send(page, 'text/html; charset=utf-8')
            elif path == '/api/config':
                self._send(json.dumps({'keys': KEYS, 'led_of': panel.led_of}).encode())
            elif path == '/api/state':
                self._send(json.dumps(panel.state()).encode())
            elif path == '/api/audio':
                start, end, chunk = panel.audio_from(int(q.get('pos', '-1')))
                self._send(chunk, 'application/octet-stream', [('X-Start', str(start)), ('X-End', str(end))])
            else:
                self.send_error(404)

        def do_POST(self):
            n = int(self.headers.get('Content-Length') or 0)
            body = json.loads(self.rfile.read(n) or b'{}')
            if self.path == '/api/button':
                panel.button(body['code'], body['down'])
            elif self.path == '/api/turn':
                panel.turn(body['enc'], body['d'], body.get('repeat', 1))
            else:
                self.send_error(404)
                return
            self._send(b'{}')

    ThreadingHTTPServer(('0.0.0.0', port), H).serve_forever()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('run_dir')
    ap.add_argument('--port', type=int, default=8080)
    ap.add_argument('--map', help="EUC+'s symbol map (eucplus.syx.map.json): show the notes it chooses")
    ap.add_argument('--pcm', default=os.path.join(os.environ.get('TMPDIR', '/tmp'), 'emulab-live.f32'))
    a = ap.parse_args()
    panel = Panel(a.run_dir, a.pcm, a.map)
    print('panel: http://localhost:%d' % a.port, flush=True)
    serve(panel, a.port)


if __name__ == '__main__':
    main()
