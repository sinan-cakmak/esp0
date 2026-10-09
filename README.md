# Five-switch WiZ bulb controller

An ESP32 controller for six living-room WiZ bulbs using five maintained toggle switches. Switch 1 controls a pair; switches 2–5 control one bulb each. This is a learning prototype being checked before permanent assembly.

See [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) for confirmed hardware, review findings, and test progress, and [WIRING.md](WIRING.md) before changing connections.

## Configuration

- WiFi: copy `main/wifi_config.h.example` to `main/wifi_config.h` and fill in credentials. This file is ignored by Git; do not share it.
- GPIO, bulb MAC addresses, and switch direction: `main/controller_config.h`.
- Bulb IP addresses are discovered by MAC, not hard-coded. Discovery runs at connection/reconnection, and retries every 30 seconds while bulbs are missing or unconfirmed.
- Intended room labels and physical lever directions still need verification.

Current assignments, preserved from the old firmware:

| Switch | GPIO | Bulb MAC addresses | Bulb ON when input is |
| --- | --- | --- | --- |
| 1 | 4 | `444f8e26e756`, `444f8e26e796` | LOW (contacts closed to GND) |
| 2 | 5 | `d8a01162bc9e` | HIGH (contacts open) |
| 3 | 18 | `d8a01162ba16` | HIGH (contacts open) |
| 4 | 19 | `444f8e308782` | HIGH (contacts open) |
| 5 | 21 | `d8a01170b374` | HIGH (contacts open) |

The mixed direction is inherited configuration, not a requirement. Do not assume that a physical lever marked ON corresponds to these electrical states.

## First powered test: inputs only

1. Activate a working ESP-IDF 5.5.1 environment.
2. Run `idf.py menuconfig`. Under **WiZ controller**, enable **Switch input test only (no WiFi or bulb commands)**.
3. Build and flash: `idf.py -p /dev/cu.usbserial-0001 build flash monitor`. Check the port if it changes.
4. Confirm the message `INPUT TEST ONLY: WiFi disabled, no bulb commands`.
5. Leave all switches untouched for 30 seconds: there should be no new `RAW` or `STABLE` changes after initial settling.
6. Move one switch at a time, waiting one second between movements. Only that switch's `STABLE` input should change. Repeat each switch ten times.
7. If another switch changes, or a stationary switch changes, stop and inspect the contact pairs, independent signal wires, and shared ground. Short bursts of `RAW` changes on the moved switch may be normal contact bounce; the stable result should settle once.
8. Exit the monitor with Ctrl+]. Record results in `PROJECT_CONTEXT.md`.

This mode does not need WiFi credentials and does not initialize WiFi or send bulb commands. Snapshot levels are ordered switch 1 through 5; the validity flags show whether each has settled.

## Normal operation (after inputs pass)

Disable the input-only option, configure WiFi if needed, then build/flash again. Bulbs need mains power and must be accessible on the same local network; the switches carry GPIO signals only.

- Inputs are sampled every 10 ms, with 50 ms of unchanged samples required to accept an initial state or change.
- A separate task handles bulb communication. Network timeouts do not block switch sampling.
- Startup and reconnection discover bulbs before applying settled switch positions.
- Each bulb is queried and its MAC checked before a state command. Success requires a read-back of the matching bulb's actual state.
- Commands retry up to three times. An unconfirmed address is discarded and rediscovered.
- A full scan starts approximately two seconds after the previous scan completes. Unreachable bulbs may delay a scan.
- **Maintained-switch behavior:** normal mode makes the bulbs follow the switches. An app/schedule change can be overridden by the next scan. This behavior is intentional here but should be confirmed before final assembly.
- GPIO2 LED: HIGH after a complete successful scan; LOW at startup, on lost WiFi, or on a failed scan. It is a coarse status indicator, not a blink-code interface. Not all boards have an LED on GPIO2.

Live tests still needed: correct room mapping and direction, independent control, all switch combinations, controller reboot, bulb power loss/return, WiFi loss/return, and sustained operation.

## Firmware checks

```bash
bash tests/run_host_tests.sh
idf.py build
```

Host regression tests cover contact bounce, initial settling, independent inputs, clock wrap, polarity, MAC normalization/identity, malformed/error responses, and duplicate assignments. They use the bundled ESP-IDF cJSON implementation with address/undefined-behavior sanitizers. Set `IDF_PATH` if ESP-IDF is installed elsewhere.

A successful build or host test does not validate the physical wiring or bulb behavior.

## Files

- `main/main.c`: input sampler, network worker, discovery, communication, startup.
- `main/controller_config.h`: editable GPIO/MAC/polarity assignments.
- `main/controller_logic.c`: debounce and switch-direction logic.
- `main/wiz_protocol.c`: MAC and response validation.
- `main/Kconfig.projbuild`: switch-only diagnostic setting.
- `AGENTS.md`: working instructions for future coding sessions.
- `materials.csv`: user's available materials; not a wiring schematic.

GPIO5 and GPIO2 are ESP32 strapping pins. Review their physical connections before soldering; do not change GPIO assignments without moving the matching wire. See [Espressif GPIO reference](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/gpio.html). WiZ UDP message examples are documented by [pywizlight](https://github.com/sbidy/pywizlight).
