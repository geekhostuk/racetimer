# Parts list

What one gate needs. For several gates, multiply the per-gate parts; the
tools and the debug extras are needed only once. Wiring and power details
are in [hardware.md](hardware.md).

## Per gate

| Qty | Part | Notes |
|---:|---|---|
| 2 | **Seeed XIAO ESP32-C5** | The receivers, top bar and bottom bar. Flashed with Widscan58's `esp32c5_uart` build. Any ESP32-C5 board can work, but the pins and docs here assume the XIAO. |
| 1 | **ESP32-WROOM dev board** (30-pin "ESP32 DevKit V1" style) | The gate controller. Any classic ESP32 board with GPIO16, 17, 26 and 27 broken out works. |
| 2 | **5.8 GHz antennas** for the C5s | Same type on both bars. Omnis for a first test; for the field, small patches facing into the gate are expected to work better ([hardware.md](hardware.md#antennas)). Match the pilots' VTX polarisation (RHCP or LHCP). Check what antenna connector your XIAO has (u.FL/IPEX) and buy antennas or pigtails to match. |
| 2 | **Electrolytic capacitor, 220–470 µF, 10 V or higher** | **Required.** One across each C5's VBUS and GND, right at the XIAO. Without it the C5 resets in a loop at radio start-up (`RTC_PWR_GLITCH`); see [Power](#power). A low-ESR type is best; 470 µF / 16 V is a safe choice. |
| 2 | **4-core cable, up to 2 m, 28 AWG or thicker** | One per receiver: TX, RX, +5 V, GND. 26 or 24 AWG gives more margin. With 6-core cable, double up +5 V and GND. |
| 1 | **5 V supply, 2 A or more** | A USB power bank or USB charger. Needs to hold 5.0–5.2 V under load. A laptop USB port is too weak for all three boards. |
| 1 | **USB power breakout** (USB-A or USB-C plug to screw terminals), or a cut USB cable | Takes 5 V and GND from the supply to the C5 cables and the WROOM. Powers the C5s directly, not through the WROOM's VIN pin. |
| — | **Connectors** at the controller end | Dupont housings, JST-XH, or screw terminals, so a receiver cable can be unplugged. |
| — | Heat shrink, cable ties | For the joints at the XIAO and for strain relief on the gate frame. |
| 1 | Enclosure for the controller (optional) | Keeps the WROOM and the power wiring off the ground. |
| 2 | Small enclosures or mounts for the C5s (optional) | Keep the antenna clear of metal. Mount the patch facing into the gate. |

## Power

| Board | Current |
|---|---|
| Each XIAO ESP32-C5 | ~100 mA steady; short spikes of several hundred mA when the radio starts |
| ESP32-WROOM, Wi-Fi access point on | ~150 mA typical; ~300–400 mA peaks while transmitting |
| **Total per gate** | **~350 mA typical, ~1 A worst-case peaks** |

- A 10,000 mAh power bank runs a gate for more than a day.
- **Don't power the C5s from the WROOM board's VIN pin.** That pin
  is the USB 5 V after a diode (~4.5 V). In testing, a C5 fed from it reset
  every ~200 ms with `RTC_PWR_GLITCH`, and the same C5 ran fine on its own
  USB. Feed each C5 from the 5 V supply through its own cable, with the
  capacitor at the C5 end.
- Voltage drop on 2 m of 28 AWG (≈0.85 Ω for 5 V out plus GND back):
  ~0.1 V at 100 mA steady, ~0.4 V during spikes. The capacitor covers the
  spikes.

## Tools

| Item | Used for |
|---|---|
| Soldering iron, solder | The XIAO pins and the capacitors |
| Multimeter | Checking 5 V at each C5 (VBUS to GND: 4.8–5.2 V) and finding the WROOM's VIN pin |
| USB-C cable (data, not charge-only) | Flashing the C5s |
| Micro-USB or USB-C cable, to match the WROOM board | Flashing the WROOM and reading its log |

## Optional, for debugging

| Item | Used for |
|---|---|
| USB-UART adapter that does 2 Mbaud (CP2102N, CH343, FT232H; not CH340G) | Checking a receiver on its own with `tools/c5_frames.py` |
| A spare VTX, or a quad on the bench, with a low-power setting (25 mW) | Bench tests: carrying a signal past each antenna |
| Jumper wires, breadboard | Bench wiring before soldering cables |
