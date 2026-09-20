# ADR: MCU / Board Selection for the Grill Thermometer

**Status:** Accepted
**Date:** 2026-09-19

## Context

REQUIREMENTS.md defines the constraints this hardware choice must satisfy. Key drivers:

- ESP-IDF + ESP-Matter required → ESP32 family only.
- Built-in color touchscreen (UI-1, UI-2).
- Three probe interfaces: 1× Type-K chamber probe, 2× NTC food probes (PR-1…PR-4).
- 1 button (UI-3), 1 RGB LED (UI-4).
- Battery + USB power with charging (PW-1…PW-3), battery level readable (CN-7, UI-8).
- Small, compact form factor (NF-1).
- **NF-3 (new driver):** must be buildable by a third party from off-the-shelf parts — no soldering, no custom PCB, no 3D-printed enclosure.
- **NF-4 (new driver):** should support extending to more probes later without a hardware redesign.

## Decision

**Board:** M5Stack **CoreS3 SE** ([Link](https://docs.m5stack.com/en/core/M5CoreS3%20SE)) (ESP32-S3, dual-core Xtensa LX7 @ 240 MHz, 16 MB flash, 8 MB PSRAM, 2.0" 320×240 capacitive touchscreen, USB-C with OTG/CDC, AXP2101 power management, built-in microSD).

**Battery/base accessory:** M5Stack **M5GO Battery Bottom3** ([Link](https://shop.m5stack.com/products/m5go-battery-bottom3-for-cores3-only)) (500 mAh LiPo, TP4057 charge IC, snaps onto the CoreS3(SE) bottom bus, no cable).

**Sensor path:** All three probes are read over I2C, plugged into the board's native Port A (Grove/HY2.0-4P):
- Chamber probe: **M5Stack Kmeter Unit** ([Link](https://docs.m5stack.com/en/unit/kmeter)) (MAX31855-based, I2C, default address `0x66`, range −200…1350 °C) instead of a raw MAX6675/SPI module.
- 2× food probes: **Grove ADS1115** 4-channel 16-bit I2C ADC module ([Link](https://wiki.seeedstudio.com/Grove-16-bit-ADC-ADS1115/)), 2 of 4 channels used for the ThermoWorks TX-1001X-OP thermistors' voltage dividers.
- Both share the bus via a **Unit PaHub** (6-channel I2C multiplexer) ([Link](https://docs.m5stack.com/en/unit/pahub)) plugged into Port A, avoiding any address-conflict handling.

**Button & status LED:** Both are already provided by the chosen board/accessory combination, so no separate purchase is needed:
- Button: any simple momentary switch wired to the M5GO Battery Bottom3's **PORT.B** (`G8`/`G9`, the CoreS3(SE) pins reserved as `PB_IN`/`PB_OUT`).
- RGB LED: the M5GO Battery Bottom3's **built-in 10× WS2812 ring**, driven via `G5` (the bus pin the base wires as "RGB"). No separate LED unit required.

## Alternatives considered

**1. Generic ESP32-S3-DevKitC-1 + separate SPI touch display module.** Maximum flexibility, cheapest core silicon, but two loose PCBs, no built-in charging, and every peripheral needs its own wiring/enclosure work — fails NF-3 outright.

**2. Waveshare ESP32-S3-Touch-LCD-1.28 (round, integrated touch display + LiPo charging).** Close to final product shape and cheaper than the M5Stack path. Rejected in favor of M5Stack because it has no plug-and-play sensor ecosystem — MAX6675 (SPI, 4 lines) doesn't fit through a 4-pin Grove-style port, forcing manual wiring to raw header pins, which works against NF-3's "no soldering, no custom assembly" goal. Still a valid fallback if cost or the round form factor become priorities later.

**3. Chamber probe on MAX6675/SPI (original decision).** Superseded by the Kmeter Unit for this iteration: SPI needs 4 lines and doesn't route through a single Grove/I2C port, whereas keeping all three probes on one I2C bus is what makes NF-3 achievable. MAX6675 accuracy was already accepted; Kmeter Unit's MAX31855 is equal or better (±2 °C, wider range), so no requirement is weakened by the swap.

## Wiring overview

```
CoreS3 SE
 ├─ Port A (I2C, native) ──> Unit PaHub (6-channel I2C hub)
 │                             ├─ Port 1 ──> Kmeter Unit ──> Type-K probe (chamber)
 │                             ├─ Port 2 ──> Grove ADS1115 ──> 2× NTC thermistor probes
 │                             │              (ThermoWorks TX-1001X-OP via 2.5 mm jack breakout)
 │                             └─ Ports 3-6 ── spare, for future probes (see PR-9/NF-4)
 │
 └─ Bottom bus ──> M5GO Battery Bottom3
                    ├─ Battery + charging (PW-1…PW-3)
                    ├─ PORT.B (G8/G9, PB_IN/PB_OUT) ──> push button (UI-3)
                    └─ G5 ──> built-in WS2812 ring (UI-4)
```

## Parts list

| Part                                            | Role                                       | Interface                        |
| ----------------------------------------------- | ------------------------------------------ | -------------------------------- |
| M5Stack CoreS3 SE                               | Main controller, display, touch            | —                                |
| M5Stack M5GO Battery Bottom3                    | Battery, charging, button pins, status LED | Bottom bus (mechanical snap-fit) |
| M5Stack Kmeter Unit                             | Chamber probe interface (Type-K)           | I2C, `0x66`                      |
| Grove ADS1115 module                            | Food probe interface (2× NTC)              | I2C, `0x48`–`0x4B`               |
| Unit PaHub                                      | I2C bus expansion / future probes          | I2C                              |
| Push-button (momentary, Grove/HY2.0-4P pigtail) | UI-3                                       | GPIO (PORT.B)                    |
| ThermoWorks TX-1001X-OP ×2                      | Food probes                                | 2.5 mm jack → ADS1115            |
| Type-K thermocouple probe                       | Chamber probe                              | Kmeter Unit probe input          |
| Small 2.5 mm jack-to-screw-terminal breakout ×2 | Marries food probe jacks to ADS1115 inputs | passive                          |

The only non-plug-and-play assembly step is the jack-to-terminal breakout for the two food probes — everything else is standard Grove/HY2.0-4P cabling.

## Consequences

- Firmware targets ESP32-S3 exclusively via ESP-IDF/ESP-Matter.
- All three probes and future expansion sensors live on one I2C bus behind a PaHub — the sampling loop (PR-6) must poll it in a way that holds 1 Hz for all channels; see PR-9/NF-4. Up to 6 devices (one per PaHub port) is achievable with straightforward sequential polling; beyond that, concurrent/staggered polling needs to be designed in rather than retrofitted.
- The RGB status LED is now the base accessory's shared ring rather than a dedicated single LED — visually similar (can be shown as one color across all 10 pixels) but tie the firmware's LED driver to `G5`/WS2812 timing, not a simple GPIO toggle.
- Physical footprint grows compared to a bare integrated board (CoreS3 SE + Battery Bottom3 + PaHub + probe modules), trading a smaller/cheaper single-PCB design for full off-the-shelf buildability (NF-3).

## Open follow-ups

- Confirm the M5GO Battery Bottom3 is validated for CoreS3 SE specifically (marketing copy says "for CoreS3 only"; mechanically identical footprint, but not explicitly confirmed for SE in official docs).
- Confirm final GPIO/I2C pin assignment against schematics once hardware is in hand.
- Decide and document the concurrent-polling approach for the sampling loop before probe count grows past ~6 (NF-4).
