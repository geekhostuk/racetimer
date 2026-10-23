# Hardware

Per gate: two Seeed XIAO ESP32-C5 receivers, one ESP32-WROOM dev board, a
5 V supply, and antennas for the receivers.

## Wiring

| From | To | Note |
|---|---|---|
| Top C5 D6 (GPIO11, TX) | WROOM GPIO16 (UART2 RX) | |
| Top C5 D7 (GPIO12, RX) | WROOM GPIO17 (UART2 TX) | |
| Bottom C5 D6 (GPIO11, TX) | WROOM GPIO26 (UART1 RX) | |
| Bottom C5 D7 (GPIO12, RX) | WROOM GPIO27 (UART1 TX) | |
| All GNDs | common ground | needed |
| 5 V | each board's 5 V / VIN | or power the C5s over USB |

- **Check the XIAO pin numbers on your board** before wiring: D6 = GPIO11
  and D7 = GPIO12 are from the XIAO ESP32-C5 pinout and not yet checked on
  hardware. They are set in `third_party/widscan58/firmware/platformio.ini`
  (env `esp32c5_uart`, `LINK_UART_TX` / `LINK_UART_RX`). The WROOM pins are in
  `gate/src/main.c`.
- The link runs at 2 Mbaud. Keep the wires short, under about 1 m. A gate's
  bottom receiver may be ~2 m away: use twisted pair with ground, or drop to
  1 Mbaud in both places if `crc` errors show in the UI's status chips (with
  R1/R3/R6/R8 the stream is ~90 KB/s, so 1 Mbaud is near its limit).
- Both chips are 3.3 V logic, so no level shifting is needed.
- The C5's USB port still works for flashing (`pio run -e esp32c5_uart -t
  upload`), but in this build the receiver's commands and data go over the
  UART only, so Widscan58's web UI can't talk to it over USB.

## Antennas

The level difference between the top and bottom bars is what tells a pass
through the gate from one over or under it.

- **Omni antennas** on both bars: the simplest. Through the middle reads
  ~0 dB, 1 m over a 2 m gate ~+10 dB. Close to a bar, inside or outside reads
  about the same, so passes near the frame are ambiguous.
- **Patch antennas pointing into the gate** (top patch pointing down, bottom
  pointing up): a quad over the gate is behind the top patch (about 15–20 dB
  weaker), so the boundary lines up with the frame. Expected to work better.
  The field test should confirm this.

Use the same antenna type and cable on both bars, then **Calibrate** in the
web UI to remove the rest of the difference between the two receivers.

## RX gain

Start at Widscan58's default, 45. A close pass must not clip: if levels
reach 254 or the UI's status shows "clipped", lower the gain by 5–10. When
both receivers clip, the top − bottom difference is lost. Then set EnterAt
and ExitAt again (Widscan58's docs/RotorHazard.md, "RX gain").

## Field test results

*To be filled in from the Milestone 4 recordings (PLAN.md).*
