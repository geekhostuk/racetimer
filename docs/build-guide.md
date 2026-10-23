# Build guide

Everything for one gate on one page: the plan, the parts, the wiring and
where things go on the gate. Details are in [parts.md](parts.md),
[hardware.md](hardware.md) and [PLAN.md](../PLAN.md).

## The idea

A normal lap timer has one receiver per gate and counts a lap at the signal
peak, so a quad flying close over or under the gate still counts. racetimer
puts a receiver on the top bar and one on the bottom bar of each gate. A pass
only counts when both see the quad at the same moment with balanced levels.

```
                         OVER the gate: top much stronger   ──► OVER, no lap
          ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─►
       ┌──────────────[TOP RX]──────────────┐
       │                                    │
       │   THROUGH: top ≈ bottom            │      ──► VALID, lap counted
  ─────┼────────────────────────────────────┼────►
       │                                    │
       └─────────────[BOTTOM RX]────────────┘
          ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─►
                         UNDER (raised gate): bottom much stronger ──► UNDER
```

| Top − bottom at the pass | Verdict |
|---|---|
| within ±6 dB (setting: Δ limit) | **VALID**: a lap |
| top much stronger | **OVER** |
| bottom much stronger | **UNDER** |
| only one receiver saw it | **SINGLE_TOP / SINGLE_BOTTOM** |

Channels: Raceband R1 (5658), R3 (5732), R6 (5843), R8 (5917) MHz.

## Plan outline

| Milestone | What | Status |
|---|---|---|
| 1. Receiver UART link | Fork of [Widscan58](https://github.com/CapnBry/widscan58) by CapnBry, branch `uart-link`: the C5 talks to the controller over a 2 Mbaud UART instead of USB (env `esp32c5_uart`) | Done; a C5 streamed ~1900 updates/s to the WROOM |
| 2. Gate controller + web UI | ESP32-WROOM reads both receivers, serves a live web UI on its own Wi-Fi | Done; tested with the top receiver |
| 3. Pass detection + verdicts | Widscan58's `lap.c` per receiver and channel, then top/bottom pairing, verdict, laps, calibration | Done in code, 14 host tests pass; not yet flown |
| 4. Field test | Fly through, over, under; record; choose antennas and thresholds | To do |

Next steps, in order:

1. Power each C5 from the 5 V supply through its cable, with the capacitor
   at the C5. Check the power (see [Checking it works](#checking-it-works)).
2. Wire the bottom C5 and check both show levels in the web UI.
3. Bench test: a low-power VTX on R1 moved past each antenna by hand.
4. Mount on a gate and fly (Milestone 4).

Later, not started: several gates reporting to one server, timing across
gates, a RotorHazard plugin.

## Parts (per gate)

| Qty | Part |
|---:|---|
| 2 | Seeed XIAO ESP32-C5 (receivers) |
| 1 | ESP32-WROOM DevKit V1, 30-pin (controller) |
| 2 | 5.8 GHz antennas, both the same; match the pilots' polarisation (RHCP/LHCP) and the XIAO's antenna connector |
| 2 | **470 µF capacitor, 10 V or higher** (220 µF minimum), one at each C5. **Required** |
| 2 | 4-core cable, up to 2 m, 28 AWG or thicker |
| 1 | 5 V supply, 2 A or more (power bank or USB charger) |
| 1 | USB power breakout or a cut USB cable, for the 5 V rail |
| — | Connectors, heat shrink, cable ties, optional enclosures |

Budget ~350 mA per gate, ~1 A peaks. Full list with notes and tools:
[parts.md](parts.md).

## Wiring

### System

```
                         ┌────────────────────────┐
                         │ 5 V supply  (≥ 2 A)    │
                         └───┬────────────────┬───┘
                           +5 V              GND
                             │                │
                ┌────────────┴────────────────┴────────────┐
                │      5 V rail and GND rail (breakout)    │
                └──┬─────────────┬─────────────────┬───────┘
                   │             │                 │
          4-core cable      VIN + GND         4-core cable
          (≤ 2 m)                │            (≤ 2 m)
                   │             │                 │
   ┌───────────────┴──┐  ┌───────┴──────────┐  ┌───┴──────────────┐
   │ TOP C5 (XIAO)    │  │ ESP32-WROOM      │  │ BOTTOM C5 (XIAO) │
   │ top bar          ├──┤ controller       ├──┤ bottom bar       │
   │                  │  │ UART2            │  │ UART1            │
   └──────────────────┘  │ GPIO16/17        │  │ GPIO26/27        │
                         └────────┬─────────┘  └──────────────────┘
                                  │ Wi-Fi AP "racegate-XXXX"
                                  ▼
                         phone / laptop: http://192.168.4.1/
```

The WROOM can also run from its own USB lead instead of VIN; its GND must
still join the GND rail.

### One receiver cable

The same for both receivers; only the WROOM pins differ.

```
  XIAO ESP32-C5                     4-core cable, ≤ 2 m                   controller end
 ┌───────────────────┐
 │ D6  (GPIO11, TX) ●┼──────── core 1 ─────────────────────────────►  WROOM RX pin
 │ D7  (GPIO12, RX) ●┼◄─────── core 2 ──────────────────────────────  WROOM TX pin
 │                   │
 │ VBUS (5 V)       ●┼───┬──── core 3 ──────────────────────────────  +5 V rail (supply)
 │                   │   │+
 │                   │ ═════  470 µF, 10 V+   (soldered at the XIAO)
 │                   │   │−   stripe side to GND
 │ GND              ●┼───┴──── core 4 ──────────────────────────────  GND rail (supply + WROOM GND)
 └───────────────────┘
```

| Core | C5 pin | Top receiver → WROOM | Bottom receiver → WROOM |
|---|---|---|---|
| 1 | D6 (GPIO11, TX) | RX2 / GPIO16 | D26 |
| 2 | D7 (GPIO12, RX) | TX2 / GPIO17 | D27 |
| 3 | VBUS (5 V) | +5 V rail (supply) | +5 V rail (supply) |
| 4 | GND | GND rail | GND rail |

TX goes to RX and RX to TX. **VBUS goes to the supply, never to the WROOM's
VIN pin**: fed from VIN, a C5 resets in a loop (`RTC_PWR_GLITCH`).

### WROOM pins

30-pin DevKit V1, chip side up, USB at the bottom. Arrows show which way the
data goes; only the labelled pins are used.

```
                             ┌──────────────┐
                             │   antenna    │
                        EN ● │              │ ● D23
               VP (GPIO36) ● │              │ ● D22
               VN (GPIO39) ● │   (not 5 V!) │ ● TX0
                       D34 ● │              │ ● RX0
                       D35 ● │              │ ● D21
                       D32 ● │              │ ● D19
                       D33 ● │              │ ● D18
                       D25 ● │              │ ● D5
  bottom C5 D6 ──►     D26 ● │              │ ● TX2 (GPIO17) ──► top C5 D7
  bottom C5 D7 ◄──     D27 ● │              │ ● RX2 (GPIO16) ◄── top C5 D6
                       D14 ● │              │ ● D4
                       D12 ● │              │ ● D2
                       D13 ● │              │ ● D15
  GND rail     ───     GND ● │              │ ● GND
  +5 V rail    ───     VIN ● │              │ ● 3V3
                             │    [USB]     │
                             └──────────────┘
```

- D26 is the WROOM's RX from the bottom C5's D6; D27 is its TX to the
  bottom C5's D7. RX2/TX2 do the same for the top C5.
- VIN is at the USB end next to GND; on some boards it looks like "VN". The
  real VN (GPIO39) is at the antenna end next to VP. Never put 5 V on it.
- Pin order varies between makers: go by the printed labels, not by
  position.

## Position on the gate

### Front view (looking along the flight path)

Sized for a common 1.5 m × 1.5 m (5 ft) gate.

```
                        ┌───────┐ top C5 in a small box on top of the bar
       ┌────────────────┤  C5   ├────────────────┐
       │                └───┬───┘                │
       │                  [ANT]  antenna under   │
       │                    ▼    the bar, facing │
       │                         down            │
  ┌────┴───┐                                     │
  │ CTRL   │ controller box on a leg,            │
  │ WROOM  │ about half way up         opening   │
  │ + 5 V  │                                     │
  └────┬───┘                                     │
       │                    ▲                    │
       │                  [ANT]  antenna on top  │
       │                ┌───┴───┐ of the bar,    │
       └────────────────┤  C5   ├ facing up ─────┘
                        └───────┘ bottom C5 under the bar
       ═══════════════════════════════════════════  ground
```

- **Centre each receiver along its bar.** Top and bottom must sit on the
  same vertical line, so a pass through the middle reads ~0 dB.
- **Antennas face into the opening**: the top one points down, the bottom
  one points up. Keep them flush with the bar; anything sticking into the
  opening gets hit.
- **Board outside, antenna inside**: put each XIAO on the outer side of its
  bar (above the top bar, below the bottom bar) and bring the antenna to the
  inner face with its pigtail. The bar then shields the antenna a little from
  passes over or under the gate.
- **Controller about half way up one leg.** Each cable then runs ~0.75 m
  along the leg and ~0.75 m along the bar: about 1.5 m, inside the 2 m
  limit. With the controller at the foot of the leg, the top cable would need
  ~2.25 m. On a bigger gate, mount the controller so neither cable is over
  2 m.
- Run each cable along the frame and tie it down every 30 cm or so, with a
  little slack at both ends so a crash doesn't pull on the solder joints.

### Side view (looking across the flight path)

```
                 top C5 ──►  ┌─┐
                             │▼│  antenna in the gate's plane
                             │ │
   flight ───────────────────┼─┼──────────────────►  the pass is timed
   direction                 │ │                     as the quad crosses
                             │▲│                     the gate's plane
              bottom C5 ──►  └─┘
```

- Mount both antennas **in the plane of the gate** (the middle of the bar's
  depth), not on the front or back edge. The pass is timed at the signal
  peak, which is when the quad crosses the antenna.
- Both antennas the same way round. If one is on the front edge and one on
  the back, the two peaks arrive at different times and the pair window
  (75 ms) can miss them.

### Antennas and the frame

- **Omni antennas** are the simplest start. Expect a pass through the middle
  ~0 dB, 1 m over the gate ~+10 dB. Close to a bar, inside and outside read
  much the same.
- **Small patch antennas facing into the opening** should work better: a quad
  over the gate is behind the top patch (15–20 dB weaker). The field test
  decides.
- PVC frames are fine. Keep antennas at least 5 cm from metal or carbon.
- A gate standing on the ground puts the bottom antenna near the grass;
  that's fine, **Calibrate** on site removes the difference.
- **Known gap:** two receivers stacked vertically catch over and under, not
  a quad flying past *beside* the gate, which can look balanced. Patches
  facing into the opening help (beside the gate is off their axis), and so
  does EnterAt. Check this in the field test.

### Several gates

Each gate is self-contained: its own controller and Wi-Fi network
(`racegate-XXXX`, the last 4 hex digits of its MAC address). Join one gate's
network at a time to watch it. Gates reporting to one server is later work.

## Checking it works

1. **Power:** with the cable and capacitor in, open the web UI, type
   `baud 0 115200` in the receiver console (`baud 1 115200` for the bottom),
   reset the C5. One boot message, then silence: good. `RTC_PWR_GLITCH`
   repeating: still browning out (check the capacitor and the supply).
   `baud 0 0` switches back.
2. **Link:** both status chips show updates arriving (~2000/s per receiver)
   and no climbing `crc` errors.
3. **Bench:** a 25 mW VTX on R1 carried past the top antenna lifts the
   orange (top) trace; past the bottom antenna, the blue one.
4. **Calibrate:** at the gate, press **Calibrate** and fly a few passes
   through the middle; it learns the offset between the two receivers.
5. **Field test:** fly through the middle, near each bar on the inside, just
   over, just under (if raised) and beside the gate. Record, then plot with
   `tools/plot_log.py` and set the Δ limit.

## Credits

Built on [Widscan58](https://github.com/CapnBry/widscan58) by CapnBry
(Bryan Mayland), which turns the ESP32-C5 into the 5.8 GHz receiver and
provides the lap detection. See the [README](../README.md#credits).
