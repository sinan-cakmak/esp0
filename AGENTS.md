# Project working instructions

Read `PROJECT_CONTEXT.md` before changing firmware or advising on assembly. Keep it updated with confirmed hardware facts, test results, and decisions.

- This is Sinan's first ESP32 learning project: five maintained toggle switches controlling six living-room WiZ bulbs, with one switch controlling a pair.
- Work iteratively. Explain clear fixes briefly using the previous behavior, the change, and why it matters. Give the next useful test rather than a large tutorial. Let the user guide subsequent implementation.
- Fix definite bugs within the requested scope. Preserve existing GPIO assignments, bulb identities, and switch polarity until the physical assembly and intended room mapping are confirmed.
- Hardware facts must be distinguished from assumptions. `materials.csv` is the user's inventory, not proof that every listed part is fitted. Do not guess the contact arrangement of a four-terminal switch.
- Keep credentials in ignored `main/wifi_config.h`; never print them or put them in documentation/tests. Do not change the user's materials list.
- Configure GPIO before sampling. Sample and debounce independently of networking. Never treat a successful UDP send as proof that a bulb changed; validate the source, method, MAC identity, and actual state.
- Use the switch-only diagnostic before live bulb tests. A build or host test does not prove wiring or physical operation. Record exactly what was tested and whether firmware was flashed.
- Do not call the assembly ready to solder until input isolation, bulb mapping, switch direction, reconnect behavior, and power/reset tests pass. Use removable board connections and retain USB access when planning packaging.
- Standard ESP32 GPIO5 and GPIO2 are strapping pins. Review their use before permanent assembly; do not silently move pins in firmware while the wiring still uses the old pins.
- Run the host regression tests and an ESP-IDF build after firmware changes. Use ESP-IDF 5.5.1 when available. Avoid unrelated tooling upgrades.
