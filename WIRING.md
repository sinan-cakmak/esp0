# Wiring and assembly notes

Five maintained switches control six bulbs through local WiFi. The switches connect only to the ESP32 GPIO inputs and ground; this controller does not switch bulb mains wiring.

## Confirmed and unconfirmed hardware

The user confirmed on 2026-10-10 that each fitted switch has **four terminals**, with two connected. `materials.csv` includes five IC150 four-terminal toggle switches and five IC152 three-terminal ON-ON switches. The fitted switches' exact model/contact diagram has not been verified.

Identify the active contact pair by measurement. A four-terminal switch may contain more than one contact pair; do not assume adjacent terminals are paired or electrically interchangeable.

## Independent input connections

Each chosen pair must connect in one lever position and disconnect in the other:

```text
GPIO 4  ---- [switch 1 verified contact pair] ---- GND
GPIO 5  ---- [switch 2 verified contact pair] ---- GND
GPIO 18 ---- [switch 3 verified contact pair] ---- GND
GPIO 19 ---- [switch 4 verified contact pair] ---- GND
GPIO 21 ---- [switch 5 verified contact pair] ---- GND
```

The GND ends share one ground bus. Each GPIO end has its own independent wire. **Do not join the GPIO signal wires together.** Leave unused switch terminals insulated/unconnected.

Internal pull-ups make a disconnected/open pair read HIGH and a pair connected to GND read LOW. No switch wire needs 5 V. Current bulb mapping and polarity are in `main/controller_config.h` and the [README](README.md); the older documentation's example IP addresses and bulb numbers were inconsistent and have been removed.

## Before the first powered test

1. Disconnect USB and any other power.
2. Use the multimeter's continuity/resistance mode to check the two terminals used on each switch. They must connect in one position and disconnect in the other. If in-circuit readings are ambiguous, isolate one contact wire for measurement.
3. Confirm each signal wire reaches only its assigned GPIO, and each ground wire reaches ESP32 GND. Check for loose contacts or exposed conductors joining neighboring wires.
4. Run the input-only firmware test described in the README. Test every switch independently before testing bulbs.

## Pin and power decisions before soldering

- Confirm the board/module marking and physical pin labels. Firmware targets the classic ESP32; the inventory lists a 38-pin ESP-32S development board. GPIO numbers are not connector position numbers.
- **GPIO5 (switch 2) is a strapping pin.** Its reset level affects a chip strapping setting; this alone does not prove the observed random behavior. Prefer reviewing a move to an available non-strapping input such as GPIO22 before permanent assembly. Update configuration and wiring together if that decision is made.
- GPIO2 is also a strapping pin and is used only as a status output. Confirm whether the board actually has an LED there. If adding an external LED, review reset behavior; use a current-limiting resistor.
- For the first tests, use the board's USB power. The final supply/battery scheme has not been chosen. Do not assume the purchased charger/regulator modules can simply be connected together.
- Longer switch wires may benefit from external pull-ups (for example 10 kΩ from each input to 3.3 V) if measurements show noise. Decide after input tests rather than adding components blindly.

These pin restrictions are documented in the [Espressif GPIO reference](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/gpio.html) and [ESP32 datasheet](https://documentation.espressif.com/esp32_datasheet_en.html).

## Assembly sequence to follow incrementally

1. Verify contact pairs and pass input isolation tests on the current prototype.
2. Verify bulb identities/room labels and decide uniform lever behavior.
3. Finalize GPIO choices, power, external pull-ups if needed, and a concrete wiring sketch.
4. Pass reboot, connectivity recovery, bulb power recovery, and extended operation tests.
5. Solder to perfboard in small sections and repeat the relevant input test after each section. Prefer a socket/removable headers for the ESP32.
6. Inspect joints and continuity with power disconnected before powering each revision.
7. Add enclosure mounting, wire strain relief, insulation, and access to USB/reset. Repeat final functional tests before closing the enclosure.

Permanent assembly is **not yet validated**. Work through these stages with the user instead of giving all assembly details at once.
