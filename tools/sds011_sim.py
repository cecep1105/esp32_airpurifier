#!/usr/bin/env python3
"""
Simulator Nova SDS011 untuk CAIR.

Sambungkan ke USB-TTL 3.3V yang terhubung ke UART1 ESP32-S3
(TX USB-TTL -> GPIO18, RX USB-TTL -> GPIO17, GND -> GND).
Simulator berperan sebagai sensor: kirim frame data, jawab perintah
(reporting mode, query, sleep/work, working period, device id, firmware).

Contoh:
  python sds011_sim.py --port COM7
  python sds011_sim.py --port /dev/ttyUSB0 --profile spike --base 12 --amp 40
  python sds011_sim.py --list

Perintah interaktif (ketik lalu Enter):
  set <pm25>          nilai tetap (profil steady)
  profile <nama>      steady | sine | ramp | spike | walk
  base <v> / amp <v>  parameter profil
  period <detik>      periode profil
  noise <v>           standar deviasi noise
  ratio <v>           rasio PM10/PM2.5
  burst               spike sekarang (asap/dupa dinyalakan)
  stop / start        hentikan / lanjutkan output (uji timeout)
  bad                 kirim 1 frame checksum salah
  status              tampilkan state
  quit
"""
import argparse
import math
import random
import sys
import threading
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial belum terpasang: pip install pyserial")

HEAD, TAIL = 0xAA, 0xAB
CMD_IN, DATA_OUT, REPLY_OUT = 0xB4, 0xC0, 0xC5


# ------------------------------------------------------------------ protokol
def checksum(data):
    return sum(data) & 0xFF


def data_frame(pm25, pm10, dev_id):
    p25 = max(0, min(9999, int(round(pm25 * 10))))
    p10 = max(0, min(9999, int(round(pm10 * 10))))
    body = [p25 & 0xFF, p25 >> 8, p10 & 0xFF, p10 >> 8, dev_id >> 8, dev_id & 0xFF]
    return bytes([HEAD, DATA_OUT] + body + [checksum(body), TAIL])


def reply_frame(cmd, d2, d3, d4, dev_id):
    body = [cmd, d2, d3, d4, dev_id >> 8, dev_id & 0xFF]
    return bytes([HEAD, REPLY_OUT] + body + [checksum(body), TAIL])


class CommandParser:
    """Rakit perintah 19 byte AA B4 ... AB dari aliran byte (dengan resync)."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk):
        out = []
        self.buf.extend(chunk)
        while True:
            i = self.buf.find(bytes([HEAD, CMD_IN]))
            if i < 0:
                # simpan byte terakhir kalau AA (awal frame terpotong)
                self.buf = self.buf[-1:] if self.buf[-1:] == bytes([HEAD]) else bytearray()
                return out
            del self.buf[:i]
            if len(self.buf) < 19:
                return out
            frame = bytes(self.buf[:19])
            if frame[18] == TAIL and checksum(frame[2:17]) == frame[17]:
                out.append(frame)
                del self.buf[:19]
            else:
                del self.buf[:1]          # bukan frame valid, geser 1 byte


# ------------------------------------------------------------------ sensor
class SimSensor:
    def __init__(self, args):
        self.lock = threading.Lock()
        self.dev_id = args.id
        self.query_mode = False
        self.working = True
        self.period_min = 0
        self.output_on = True
        self.send_bad = False
        self.profile = args.profile
        self.base = args.base
        self.amp = args.amp
        self.prof_period = args.period
        self.noise = args.noise
        self.ratio = args.ratio
        self.t0 = time.time()
        self.walk = args.base
        self.burst_t = None
        self.last = (0.0, 0.0)

    # nilai "udara" saat t
    def value(self):
        t = time.time() - self.t0
        p = self.prof_period
        if self.profile == "steady":
            v = self.base
        elif self.profile == "sine":
            v = self.base + self.amp * (0.5 + 0.5 * math.sin(2 * math.pi * t / p))
        elif self.profile == "ramp":
            v = self.base + self.amp * ((t % p) / p)
        elif self.profile == "spike":
            if self.burst_t is None or (time.time() - self.burst_t) > p:
                self.burst_t = time.time()
            v = self.base + self.amp * math.exp(-(time.time() - self.burst_t) / (p / 4))
        elif self.profile == "walk":
            self.walk += random.gauss(0, max(0.5, self.amp / 20))
            self.walk = min(max(self.walk, self.base * 0.5), self.base + self.amp)
            v = self.walk
        else:
            v = self.base
        # burst manual di atas profil apa pun
        if self.profile != "spike" and self.burst_t is not None:
            dt = time.time() - self.burst_t
            v += self.amp * math.exp(-dt / 30.0)
            if dt > 180:
                self.burst_t = None
        v += random.gauss(0, self.noise)
        pm25 = max(0.0, min(999.9, v))
        pm10 = max(pm25, min(999.9, pm25 * self.ratio + random.gauss(0, self.noise)))
        return round(pm25, 1), round(pm10, 1)

    def make_data(self):
        pm25, pm10 = self.value()
        self.last = (pm25, pm10)
        f = data_frame(pm25, pm10, self.dev_id)
        if self.send_bad:
            self.send_bad = False
            f = f[:8] + bytes([(f[8] + 1) & 0xFF]) + f[9:]
        return f

    def handle(self, frame):
        """Proses perintah 19 byte, kembalikan list (bytes, keterangan)."""
        cmd, d2, d3 = frame[2], frame[3], frame[4]
        target = (frame[15] << 8) | frame[16]
        if target not in (0xFFFF, self.dev_id):
            return [(None, f"cmd {cmd} untuk id {target:04X}, diabaikan")]
        did = self.dev_id
        if cmd == 2:
            if d2 == 1:
                self.query_mode = bool(d3)
            return [(reply_frame(2, d2, int(self.query_mode), 0, did),
                     f"reporting mode -> {'query' if self.query_mode else 'active'}")]
        if cmd == 4:
            if not self.working:
                return [(None, "query saat sleep, diabaikan")]
            return [(self.make_data(), "query -> data")]
        if cmd == 5:
            new_id = (frame[13] << 8) | frame[14]
            self.dev_id = new_id
            return [(reply_frame(5, 0, 0, 0, new_id), f"device id -> {new_id:04X}")]
        if cmd == 6:
            if d2 == 1:
                self.working = bool(d3)
            return [(reply_frame(6, d2, int(self.working), 0, did),
                     f"sleep/work -> {'work' if self.working else 'sleep'}")]
        if cmd == 7:
            return [(reply_frame(7, 26, 10, 14, did), "firmware version")]
        if cmd == 8:
            if d2 == 1:
                self.period_min = min(30, d3)
            return [(reply_frame(8, d2, self.period_min, 0, did),
                     f"working period -> {self.period_min} menit")]
        return [(None, f"cmd tidak dikenal {cmd}")]


# ------------------------------------------------------------------ loop
def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def serial_loop(ser, sim, stop_evt):
    parser = CommandParser()
    next_tx = time.time()
    while not stop_evt.is_set():
        try:
            chunk = ser.read(64)
        except serial.SerialException as e:
            log(f"serial error: {e}")
            stop_evt.set()
            return
        with sim.lock:
            for frame in parser.feed(chunk) if chunk else []:
                for out, note in sim.handle(frame):
                    log(f"<- {frame.hex(' ')}  ({note})")
                    if out is not None and sim.output_on:
                        ser.write(out)
                        if out[1] == DATA_OUT:
                            log(f"-> PM2.5={sim.last[0]:6.1f}  PM10={sim.last[1]:6.1f}  (query)")

            now = time.time()
            interval = 1.0 if sim.period_min == 0 else sim.period_min * 60.0
            if not sim.query_mode and sim.working and now >= next_tx:
                next_tx = now + interval
                if sim.output_on:
                    ser.write(sim.make_data())
                    log(f"-> PM2.5={sim.last[0]:6.1f}  PM10={sim.last[1]:6.1f}")


def console_loop(sim, stop_evt):
    while not stop_evt.is_set():
        try:
            line = input()
        except (EOFError, KeyboardInterrupt):
            stop_evt.set()
            return
        p = line.strip().split()
        if not p:
            continue
        c, a = p[0].lower(), p[1:]
        try:
            with sim.lock:
                if c == "quit":
                    stop_evt.set()
                elif c == "set" and a:
                    sim.profile, sim.base = "steady", float(a[0])
                elif c == "profile" and a and a[0] in ("steady", "sine", "ramp", "spike", "walk"):
                    sim.profile, sim.t0, sim.walk, sim.burst_t = a[0], time.time(), sim.base, None
                elif c == "base" and a:
                    sim.base = float(a[0])
                elif c == "amp" and a:
                    sim.amp = float(a[0])
                elif c == "period" and a:
                    sim.prof_period = max(5.0, float(a[0]))
                elif c == "noise" and a:
                    sim.noise = max(0.0, float(a[0]))
                elif c == "ratio" and a:
                    sim.ratio = max(1.0, float(a[0]))
                elif c == "burst":
                    sim.burst_t = time.time()
                elif c == "stop":
                    sim.output_on = False
                elif c == "start":
                    sim.output_on = True
                elif c == "bad":
                    sim.send_bad = True
                elif c != "status":
                    print("perintah tidak dikenal (lihat docstring)")
                    continue
                print(f"profile={sim.profile} base={sim.base} amp={sim.amp} period={sim.prof_period}s "
                      f"noise={sim.noise} ratio={sim.ratio} | mode={'query' if sim.query_mode else 'active'} "
                      f"working={sim.working} output={'on' if sim.output_on else 'OFF'} id={sim.dev_id:04X}")
        except ValueError:
            print("argumen harus angka")


def main():
    ap = argparse.ArgumentParser(description="Simulator sensor Nova SDS011")
    ap.add_argument("--port", help="port serial, mis. COM5 atau /dev/ttyUSB0")
    ap.add_argument("--list", action="store_true", help="tampilkan port serial")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--profile", default="sine", choices=["steady", "sine", "ramp", "spike", "walk"])
    ap.add_argument("--base", type=float, default=10.0, help="PM2.5 dasar (ug/m3)")
    ap.add_argument("--amp", type=float, default=30.0, help="amplitudo profil (ug/m3)")
    ap.add_argument("--period", type=float, default=120.0, help="periode profil (detik)")
    ap.add_argument("--noise", type=float, default=0.8)
    ap.add_argument("--ratio", type=float, default=1.4, help="PM10/PM2.5")
    ap.add_argument("--id", type=lambda x: int(x, 16), default=0xA1B2, help="device id hex")
    args = ap.parse_args()

    if args.list or not args.port:
        for p in serial.tools.list_ports.comports():
            print(f"{p.device:15s} {p.description}")
        if not args.port:
            print("\ngunakan --port <port>")
        return

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    # DTR/RTS low (aman juga bila suatu saat dipakai di port auto-reset DevKitC)
    ser.dtr = False
    ser.rts = False
    ser.open()
    log(f"SDS011 sim di {args.port} @ {args.baud}, profil {args.profile}. Ketik perintah, 'quit' untuk keluar.")

    sim = SimSensor(args)
    stop_evt = threading.Event()
    t = threading.Thread(target=serial_loop, args=(ser, sim, stop_evt), daemon=True)
    t.start()
    try:
        console_loop(sim, stop_evt)
    finally:
        stop_evt.set()
        t.join(timeout=1)
        ser.close()


if __name__ == "__main__":
    main()
