# Wiring Guide - 5 Switch WiZ Bulb Controller

## Overview

This ESP32 project controls 6 WiZ smart bulbs using 5 physical toggle switches. One switch controls 2 bulbs together.

## Switch Configuration

| Switch # | GPIO Pin | Controls Bulbs (IP addresses)            |
| -------- | -------- | ---------------------------------------- |
| Switch 1 | GPIO 4   | 192.168.1.2, 192.168.1.3 (both together) |
| Switch 2 | GPIO 5   | 192.168.1.4                              |
| Switch 3 | GPIO 18  | 192.168.1.5                              |
| Switch 4 | GPIO 19  | 192.168.1.6                              |
| Switch 5 | GPIO 21  | 192.168.1.7                              |

## Wiring Instructions

### ESP32 Pinout Reference

```
ESP32 DevKit Pin Layout:
┌─────────────────────────┐
│  [USB]                  │
│                         │
│  GPIO 4  ──────────────│  ← Switch 1
│  GPIO 5  ──────────────│  ← Switch 2
│  GPIO 18 ──────────────│  ← Switch 3
│  GPIO 19 ──────────────│  ← Switch 4
│  GPIO 21 ──────────────│  ← Switch 5
│  GPIO 2  ──────────────│  ← Status LED (optional)
│                         │
│  GND     ──────────────│  ← Common ground
│  3.3V    ──────────────│  (not used)
└─────────────────────────┘
```

### Toggle Switch Wiring

Each toggle switch has 2 terminals:

- **Terminal 1**: Connect to the corresponding GPIO pin
- **Terminal 2**: Connect to **GND** (common ground for all switches)

**Important**: All switches share the same GND connection.

### Wiring Diagram

```
ESP32                    Toggle Switches
─────────────────────────────────────────
GPIO 4  ────────────────┐
                        │
GPIO 5  ────────────────┤
                        │
GPIO 18 ────────────────┤
                        │
GPIO 19 ────────────────┤
                        │
GPIO 21 ────────────────┤
                        │
GND     ────────────────┴───────┬─────────┬─────────┬─────────┬─────────
                                 │         │         │         │
                            Switch 1  Switch 2  Switch 3  Switch 4  Switch 5
                            Terminal 2 Terminal 2 Terminal 2 Terminal 2 Terminal 2
```

### Detailed Connection Table

**CRITICAL: Each switch has 2 terminals that MUST be connected:**

| Component    | ESP32 Pin | Switch Terminal | Notes                          |
| ------------ | --------- | --------------- | ------------------------------ |
| **Switch 1** | GPIO 4    | **Terminal 1**  | Controls bulbs 2 & 3           |
| **Switch 1** | GND       | **Terminal 2**  | Common ground (daisy-chained)  |
| **Switch 2** | GPIO 5    | **Terminal 1**  | Controls bulb 4                |
| **Switch 2** | GND       | **Terminal 2**  | Common ground (daisy-chained)  |
| **Switch 3** | GPIO 18   | **Terminal 1**  | Controls bulb 5                |
| **Switch 3** | GND       | **Terminal 2**  | Common ground (daisy-chained)  |
| **Switch 4** | GPIO 19   | **Terminal 1**  | Controls bulb 6                |
| **Switch 4** | GND       | **Terminal 2**  | Common ground (daisy-chained)  |
| **Switch 5** | GPIO 21   | **Terminal 1**  | Controls bulb 7                |
| **Switch 5** | GND       | **Terminal 2**  | Common ground (daisy-chained)  |
| Status LED   | GPIO 2    | Anode (+)       | Optional - for visual feedback |
| Status LED   | GND       | Cathode (-)     | Through 220Ω resistor          |

### Switch Operation

- **Toggle ON** (switch closed): GPIO reads LOW (0) → Bulbs turn OFF
- **Toggle OFF** (switch open): GPIO reads HIGH (1) → Bulbs turn ON

**Note**: The logic is inverted - when the switch is physically ON (closed to GND), the bulbs are OFF, and vice versa.

### Status LED (Optional)

If you want visual feedback:

- Connect LED anode (+) to GPIO 2 through a 220Ω resistor
- Connect LED cathode (-) to GND

The LED will blink to indicate:

- 1 blink = Command sent successfully
- 2 blinks = Error sending command
- 3 blinks = WiFi not connected

## Power Requirements

- ESP32 can be powered via USB or external 5V supply
- Toggle switches draw minimal current (just GPIO input)
- No additional power supply needed for switches

## Safety Notes

1. **Double-check connections** before powering on
2. **Verify GND connections** - all switches must share the same GND
3. **Check GPIO pins** - make sure you're using the correct pins (4, 5, 18, 19, 21)
4. **Test one switch at a time** initially to verify each works correctly

## Troubleshooting

### Switch not working:

- Check GND connection is secure
- Verify GPIO pin number matches the switch
- Check switch continuity with multimeter

### Bulbs not responding:

- Verify WiFi connection (check serial monitor)
- Confirm bulb IP addresses are correct in code
- Check that bulbs are on the same WiFi network

### Multiple switches triggering:

- Check for loose GND connections
- Verify no short circuits between GPIO pins
- Ensure switches are properly isolated

## Quick Test Procedure

1. Power on ESP32
2. Wait for WiFi connection (check serial monitor)
3. Toggle Switch 1 - should control bulbs at 192.168.1.2 and 192.168.1.3
4. Toggle Switch 2 - should control bulb at 192.168.1.4
5. Continue testing each switch individually
6. Verify all switches work independently
