# racetimer

**Two-receiver FPV race gates that know whether the quad flew *through* the gate.**

A normal lap timer has one receiver per gate and counts a lap at the signal
peak, so a quad flying close over or under the gate still counts. racetimer
puts two receivers on each gate, one on the top bar and one on the bottom bar,
and only counts a pass when both see the quad strongly at the same moment with
balanced levels:

| Top vs bottom at the pass | Verdict |
|---|---|
| within ±Δ limit (default 6 dB) | **VALID**, through the gate: a lap |
| top much stronger | **OVER** |
| bottom much stronger | **UNDER** |
| only one bar saw it | **SINGLE_TOP / SINGLE_BOTTOM** |

> **Status:** early and untested on a real gate. Everything builds and the
> fusion logic has host tests, but no quad has flown through one yet. The
> field test (PLAN.md, Milestone 4) decides the antennas and thresholds.

## How it works

Per gate:

```
  top bar:    XIAO ESP32-C5 (Widscan58, UART build) ──UART 2 Mbaud──┐
                                                                    ├─► ESP32-WROOM ──Wi-Fi──► phone / laptop
  bottom bar: XIAO ESP32-C5 (Widscan58, UART build) ──UART 2 Mbaud──┘    gate controller        web UI
```

- **Receivers:** [Widscan58](https://github.com/CapnBry/widscan58) turns the
  ESP32-C5's Wi-Fi radio into a 5.8 GHz FPV receiver. Each one scans the
  race channels (default Raceband R1, R3, R6, R8, ~500 updates per second per
  channel) and streams levels to the gate controller. We use our fork,
  [geekhostuk/widscan58](https://github.com/geekhostuk/widscan58) branch
  `uart-link`, which adds a UART host link (firmware env `esp32c5_uart`),
  since the WROOM can't be a USB host. It's a git submodule at
  `third_party/widscan58`.
- **Gate controller** (`gate/`, ESP32-WROOM, ESP-IDF): sets up both
  receivers, runs Widscan58's own pass detection (`lap.c`) per receiver and
  channel, pairs the top and bottom passes, decides the verdict, and serves a
  live web UI from its own Wi-Fi access point.

## Quick start

```sh
git clone --recurse-submodules https://github.com/geekhostuk/racetimer
uv tool install platformio          # or pip install platformio

# Receivers (twice: top and bottom). XIAO ESP32-C5 on USB:
cd racetimer/third_party/widscan58/firmware
pio run -e esp32c5_uart -t upload

# Gate controller. ESP32-WROOM dev board on USB:
cd ../../../gate
pio run -e esp32dev -t upload
```

Wire them as in [docs/hardware.md](docs/hardware.md), power up, join the
Wi-Fi network `racegate-XXXX` (password `racegate`) and open
http://192.168.4.1/.

The web UI shows each channel's top and bottom levels live, every pass with
its verdict and top − bottom in dB, laps, the settings (gain, EnterAt/ExitAt,
Δ limit, offset, pair window, minimum lap), a **Calibrate** button (fly a few
passes through the middle and it learns the two receivers' offset), a
console to the receivers, and **Record**, which saves the session as CSV for
`tools/plot_log.py`.

## Tools

| | |
|---|---|
| `uv run tools/gate_sim.py` | A pretend gate: open http://localhost:8058/ to try the web UI with no hardware. |
| `uv run tools/c5_frames.py /dev/ttyUSB0` | Check one receiver's UART link through a USB-UART adapter (2 Mbaud): updates per second per channel, levels, CRC errors. |
| `uv run tools/plot_log.py gate-….csv` | Plot a recording: levels with pass markers, and top − bottom per verdict, to set the Δ limit. |

## Tests

```sh
cd gate && pio test -e native       # pairing, verdicts, laps, calibration
```

## Credits

This project stands on **[Widscan58](https://github.com/CapnBry/widscan58)**
by **CapnBry** (Bryan Mayland): the ESP32-C5 5.8 GHz receiver firmware, its
frame protocol, and the lap detection (`lap.c`) the gate controller runs.
racetimer would not exist without it. Go look at it, and its live web UI at
https://capnbry.github.io/widscan58/.

Widscan58 itself builds on [C5VRX](https://github.com/Twotoz/C5VRX) and
[esp-sdr](https://github.com/ESPARGOS/esp-sdr), which found the debug path
that lets an ESP32 capture raw I/Q samples from its own radio.

## License

GPL-3.0-or-later (see [LICENSE](LICENSE)), the same as Widscan58, whose code
the gate firmware compiles in.
