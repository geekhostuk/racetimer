#!/usr/bin/env python3
# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial"]
# ///
"""Read a Widscan58 receiver on its UART link (firmware env esp32c5_uart)
through a USB-UART adapter, and show what the gate controller would get:
updates per second per channel, the latest level, and CRC errors.

    uv run tools/c5_frames.py /dev/ttyUSB0            # set the board up, then watch
    uv run tools/c5_frames.py /dev/ttyUSB0 --no-setup

Wire the adapter's RX to the C5's TX (XIAO D6), its TX to the C5's RX (D7),
and ground. The adapter must do 2 Mbaud (CP2102N, CH343, FT232H do; a
CH340G doesn't). GPL-3.0-or-later.
"""
import argparse
import struct
import sys
import time
import zlib

import serial

SYNC0, SYNC1 = 0xA5, ord("R")
FRAME_LEVELS, FRAME_TELEMETRY = 6, 3
# frames.h: frame_levels_t (32 bytes), frame_level_t (4 bytes)
LEVELS_HDR = struct.Struct("<IIIIIIHBBBBBB")
LEVEL = struct.Struct("<BBBB")
TELEMETRY = struct.Struct("<IIIIIIIIIIIHIIIB")

SETUP = [
    "PROTOCOL MONITOR", "SCAN STOP", "CHANNELS {channels}", "MODE GROUP", "FFT 64", "CAPN 256",
    "SPEC 0 256 40000000 MAX", "METRICS RSSI", "GAIN MANUAL {gain}", "SCAN START",
]


class Parser:
    def __init__(self):
        self.buf = bytearray()
        self.crc_errors = 0

    def feed(self, data):
        """Yield ("line", text) and ("frame", type, payload)."""
        self.buf += data
        while self.buf:
            if self.buf[0] == SYNC0:
                if len(self.buf) < 6:
                    return
                _, s1, ftype, ver, length = struct.unpack_from("<BBBBH", self.buf)
                if s1 != SYNC1 or ver != 1 or length > 1024:
                    del self.buf[0]
                    continue
                total = 6 + length + 4
                if len(self.buf) < total:
                    return
                (crc,) = struct.unpack_from("<I", self.buf, 6 + length)
                if zlib.crc32(bytes(self.buf[: 6 + length])) != crc:
                    self.crc_errors += 1
                    del self.buf[0]
                    continue
                payload = bytes(self.buf[6 : 6 + length])
                del self.buf[:total]
                yield ("frame", ftype, payload)
            else:
                nl = self.buf.find(b"\n")
                sync = self.buf.find(bytes([SYNC0]))
                if nl < 0 or (0 <= sync < nl):
                    if sync > 0:
                        del self.buf[:sync]  # junk before a frame (boot ROM output)
                        continue
                    if nl < 0:
                        if len(self.buf) > 4096:
                            self.buf.clear()
                        return
                line = bytes(self.buf[:nl]).decode("ascii", "replace").strip()
                del self.buf[: nl + 1]
                if line:
                    yield ("line", line)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=2000000)
    ap.add_argument("--channels", default="R1=5658 R3=5732 R6=5843 R8=5917")
    ap.add_argument("--gain", type=int, default=45)
    ap.add_argument("--no-setup", action="store_true", help="don't send the setup commands")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this long (0: run until Ctrl-C)")
    args = ap.parse_args()
    names = [c.split("=")[0] for c in args.channels.split()]

    port = serial.Serial(args.port, args.baud, timeout=0.05)
    if not args.no_setup:
        for cmd in SETUP:
            port.write((cmd.format(channels=args.channels, gain=args.gain) + "\n").encode())
            time.sleep(0.03)

    p = Parser()
    counts = [0] * len(names)
    latest = [0] * len(names)
    frames = telemetry_drops = 0
    start = shown = time.monotonic()
    try:
        while True:
            for item in p.feed(port.read(8192)):
                if item[0] == "line":
                    print(f"< {item[1]}")
                    continue
                _, ftype, payload = item
                frames += 1
                if ftype == FRAME_LEVELS and len(payload) >= LEVELS_HDR.size:
                    h = LEVELS_HDR.unpack_from(payload)
                    nch = h[-1]
                    for i in range(nch):
                        cid, flags, rssi, snr = LEVEL.unpack_from(payload, LEVELS_HDR.size + i * LEVEL.size)
                        if cid < len(names):
                            counts[cid] += 1
                            latest[cid] = rssi
                elif ftype == FRAME_TELEMETRY and len(payload) >= TELEMETRY.size:
                    telemetry_drops = TELEMETRY.unpack_from(payload)[13]
            now = time.monotonic()
            if now - shown >= 1:
                dt = now - shown
                cells = "  ".join(f"{n}: {c / dt:5.0f}/s lvl {l:3d}" for n, c, l in zip(names, counts, latest))
                print(f"{cells}  | frames {frames / dt:5.0f}/s crc_err {p.crc_errors} board_drops {telemetry_drops}")
                counts = [0] * len(names)
                frames = 0
                shown = now
            if args.seconds and now - start >= args.seconds:
                break
    except KeyboardInterrupt:
        pass
    return 0 if p.crc_errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
