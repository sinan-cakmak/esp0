# Project context and test record

Last updated: 2026-10-10 (Europe/Istanbul).

## User intent and collaboration

Sinan built this first ESP32 learning prototype several months ago. It uses five physical maintained on/off switches as a living-room remote for WiZ smart bulbs (the user initially called the brand “Vis”; the repository implements WiZ). The jumper-wire prototype has operated, but is currently unusable: moving any switch can appear to turn several bulbs on/off unpredictably.

The goal is to validate firmware, wiring, and recovery behavior before soldering and packaging, so the assembly will not need dismantling afterward. Review independently, fix definite faults, explain previous/new behavior briefly, and guide tests incrementally. Do not overwhelm the user with a full tutorial or make unrequested redesigns.

## Confirmed facts

- Repository: `/Users/sinan/esp0`; baseline reviewed at `ab8c6bc`.
- Framework: ESP-IDF 5.5.1 installation at `/Users/sinan/esp/v5.5.1/esp-idf`; target `esp32`; project tick frequency 100 Hz (10 ms per tick).
- Firmware configures five switches and six unique WiZ bulbs, one group of two and four singles.
- User confirmed that all fitted switches have four terminals, with two connected on each. Contact pairing and exact switch model still need measurement.
- `materials.csv` is the user's available inventory. Relevant items include two 38-pin ESP-32S boards, five IC150 four-terminal toggle switches, five IC152 three-terminal ON-ON switches, Mestek DM-90E multimeter, Sunline 926 soldering iron, solder, perfboards, jumper leads, and 10 kΩ/220 Ω/330 Ω resistors. Preserve this file.
- `/dev/cu.usbserial-0001` was present during the audit. This proves a serial device was visible, not the board identity or flashed firmware version.
- WiFi credentials are in ignored `main/wifi_config.h`; do not reproduce them.
- No physical GPIO reassignment or bulb reassignment was made during the audit.

## Current assignments (preserved from actual old code)

| Switch | GPIO | Bulb identities (MAC) | Electrical input that means bulb ON |
| --- | --- | --- | --- |
| 1 | 4 | `444f8e26e756`, `444f8e26e796` | LOW |
| 2 | 5 | `d8a01162bc9e` | HIGH |
| 3 | 18 | `d8a01162ba16` | HIGH |
| 4 | 19 | `444f8e308782` | HIGH |
| 5 | 21 | `d8a01170b374` | HIGH |

LOW means the selected switch contacts connect the GPIO to GND. HIGH means those contacts are open and the pull-up raises the input. Physical lever directions and the bulbs' room labels are unverified. The previous README listed different bulb assignments; its table was not a trustworthy source.

## Audit findings and fixes

1. **Startup race:** the previous bulb-handler task started before GPIO configuration, then attempted synchronization while pins were not yet inputs. Espressif documents that an unconfigured/non-input pin reads zero. Initialization simultaneously changed shared state. GPIO is now configured first; settled inputs become valid after 50 ms; a separate sampler owns input detection.
2. **Ineffective debounce:** five reads were separated by `pdMS_TO_TICKS(5)`, which evaluates to zero with this project's 100 Hz tick. The new sampler takes samples every 10 ms and requires 50 ms without a sampled change. Each switch has independent debounce state.
3. **Assumed bulb states:** GPIO initialization filled the bulb-state cache with the desired state before any confirmation. Periodic sync compared that cache, not actual bulbs. The new worker reads the real bulb state and confirms writes by read-back; startup uses the same path.
4. **UDP false success/reply mixups:** a successful `sendto` was counted as a successful bulb update, while retries covered only local send failures. Replies were not matched to sender or bulb identity, and command acknowledgements were left unread in a shared socket. Requests now use separate sockets, filter source IP/port and method, validate MAC/state, and retry unconfirmed operations. Identity is checked before writing to an address that may have been reassigned by DHCP.
5. **Reconnection race and incomplete discovery:** WiFi event callbacks replaced the shared UDP socket while other code could use it; discovery only ran at startup; initial WiFi timeout ended `app_main` before control tasks existed. Events now only publish network status, one worker owns discovery/communications, inputs continue offline, and discovery repeats after reconnection or periodically for missing/unconfirmed bulbs.
6. **Configuration/docs:** actual assignments were moved into `main/controller_config.h`, with duplicate GPIO/MAC checks. MAC comparison accepts case and separators. README and wiring notes were corrected. The old wiring diagram visually joined GPIO signal lines; the replacement shows five independent contact pairs sharing only ground. Added an input-only diagnostic mode.

These faults can explain unreliable startup and missed commands. They do **not** prove the cause of all observed cross-switch behavior; input isolation and bulb mapping must be tested on the hardware.

## Architecture and behavior now

- GPIO is configured before tasks sample it. The input task polls/debounces independently of WiFi/UDP; accepted changes publish current positions under a lock and notify the worker.
- Normal mode uses fixed MAC identities (appropriate for known bulbs) with discovered IP addresses. It never intentionally writes to an empty address or an unverified bulb identity.
- Normal mode enforces maintained switch positions at startup/reconnection and with repeated real-state scans. App/schedule changes can be overwritten. Confirm whether this is the desired interaction policy.
- The status LED's old blink codes were removed because their delays blocked input handling. GPIO2 now shows the latest full-scan success/failure; diagnostic mode leaves it low. Confirm whether an LED is actually fitted.
- Diagnostic mode (`WiZ controller -> Switch input test only` in menuconfig) logs raw edges, debounced states, and snapshots without WiFi or bulb commands.
- A missing/unconfirmed bulb is retried through discovery after up to 30 seconds. Scans are sequential, so several unreachable bulbs can delay network reaction, while input sampling continues. This simple bounded worker is sufficient to validate a six-bulb prototype; optimize further only if bench tests show unacceptable latency.

## Verification record

- Host regression tests: **passed**, including address/undefined-behavior sanitizers. Covers initial settling, bounce rejection, independent inputs, tick wrap, polarity, MAC identity/normalization, invalid responses, and duplicate assignments. This is software simulation, not a physical isolation test.
- ESP-IDF 5.5.1 normal firmware build: **passed**, no compiler warnings; output `build/audit-normal/esp0.bin`, within the configured application partition.
- ESP-IDF 5.5.1 input-only diagnostic build: **passed**, no compiler warnings; output `build/audit-input/esp0.bin`, using its own `build/audit-input/sdkconfig` with `CONFIG_ESP0_INPUT_TEST_ONLY=y`. Verified the diagnostic application's object file has no references to WiFi/UDP operations. The regular root `sdkconfig` remains in normal mode.
- Firmware flashed during this audit: **no**.
- Physical switch isolation / bulb control / reboot / reconnect tests: **not performed yet**.
- The installed ESP-IDF Python environment points to a removed Miniforge Python. A temporary environment was created at `/private/tmp/esp0-idf-audit-venv` using IDF's requirements and existing version constraints for verification; installed SDK/compiler paths were left alone. This temporary path may not persist across system cleanup. The editor's Python setting also points to the removed installation and will need attention when setting up the user's regular workflow.
- In the managed sandbox, the optional IDF component manager failed because its process-inspection operation is restricted. Verification used `IDF_COMPONENT_MANAGER=0`; this project uses only bundled SDK components, so no managed dependencies were omitted.

Environment used for verification (temporary, not a permanent tooling setup):

```bash
export IDF_PYTHON_ENV_PATH=/private/tmp/esp0-idf-audit-venv
export PATH=/private/tmp/esp0-idf-audit-venv/bin:$PATH
export IDF_COMPONENT_MANAGER=0
. /Users/sinan/esp/v5.5.1/esp-idf/export.sh
idf.py -B build/audit-normal build
idf.py -B build/audit-input -D SDKCONFIG=/Users/sinan/esp0/build/audit-input/sdkconfig build
```

## Next stages (guide one at a time)

1. With controller power disconnected, verify each fitted switch's chosen pair makes/breaks continuity, and check independent GPIO signal wiring/common GND. Then flash input-only diagnostic firmware and check stationary stability and one-at-a-time input changes (ten cycles each).
2. Once inputs pass, test normal mode and label the actual bulbs for each MAC; decide uniform lever behavior and whether app changes should remain effective.
3. Before permanent wiring, confirm the exact board, reconsider GPIO5/optional GPIO2 strapping connections, select final power, and draw the final connection plan. GPIO22 is a candidate replacement for switch 2, but no change has been made yet.
4. Test controller reset in different switch positions, WiFi loss/return, bulb power loss/return, and longer operation.
5. Solder and test in sections, use removable ESP32 headers/socket, then add enclosure, strain relief, insulation, and USB/reset access.

No solder-ready claim can be made yet. The purchased charger/regulator/relay modules are not an established power design for this controller.

## References checked

- [Espressif GPIO documentation, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/gpio.html): input configuration, pull-ups, unconfigured-input read behavior, strapping pins.
- [ESP32 datasheet](https://documentation.espressif.com/esp32_datasheet_en.html): GPIO5/GPIO2 are strapping pins. GPIO5 affects a strapping setting; do not overstate this as proof of normal boot failure or random switching.
- [pywizlight project/protocol examples](https://github.com/sbidy/pywizlight): local UDP state commands, replies, MAC identity, and discovery.
