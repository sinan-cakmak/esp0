# Multi-Switch WiZ Bulb Controller

An ESP32-based controller for managing multiple WiZ smart bulbs using physical toggle switches. This project provides reliable, local control of up to 6 WiZ bulbs through 5 toggle switches, with automatic retry logic and periodic synchronization.

## Quick Start

1. **Configure WiFi**: Copy `main/wifi_config.h.example` to `main/wifi_config.h` and add your credentials
2. **Update Bulb IPs**: Edit the `switches` array in `main/main.c` with your bulb IP addresses
3. **Wire Switches**: Follow the [Wiring Guide](WIRING.md) to connect 5 toggle switches
4. **Build & Flash**: Use `idf.py build flash monitor` to build and flash the firmware

## WiFi Configuration Setup

**IMPORTANT**: Before building, you must configure your WiFi credentials:

1. Copy the template file to create your WiFi configuration:

   ```bash
   cp main/wifi_config.h.example main/wifi_config.h
   ```

2. Edit `main/wifi_config.h` and replace the placeholders with your actual WiFi credentials:
   ```c
   #define WIFI_SSID      "YOUR_WIFI_SSID_HERE"
   #define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD_HERE"
   ```

The `wifi_config.h` file is gitignored and will not be committed to the repository, keeping your credentials secure.

## Project Overview

This project is a **Multi-Switch WiZ Smart Bulb Controller** for ESP32. It connects to your WiFi network and allows you to control 6 WiZ smart bulbs using 5 physical toggle switches.

### Features

- **WiFi Connectivity**: Connects to your local WiFi network using credentials from `wifi_config.h`
- **Multi-Bulb Control**: Controls 6 WiZ smart bulbs via UDP protocol (port 38899)
- **5 Toggle Switches**: Physical toggle switches for intuitive bulb control
- **Reliable Detection**: Hybrid polling + interrupt system ensures all switch changes are detected
- **Automatic Retry**: Failed commands are automatically retried up to 3 times
- **Periodic Sync**: Every 2 seconds, the system syncs bulb states with switch positions
- **Status LED**: Visual feedback via status LED on GPIO 2
- **Automatic Reconnection**: Automatically reconnects to WiFi if connection is lost

### Hardware Requirements

- An ESP32 development board
- 5 physical toggle switches (SPST)
- 6 WiZ smart bulbs connected to the same WiFi network
- Optional: Status LED on GPIO 2 (many ESP32 boards have a built-in LED)

### Switch Configuration

| Switch # | GPIO Pin | Controls Bulbs (IP addresses) | Logic |
|----------|----------|-------------------------------|-------|
| Switch 1 | GPIO 4   | 192.168.1.2, 192.168.1.7 (both together) | LOW=ON, HIGH=OFF |
| Switch 2 | GPIO 5   | 192.168.1.4 | HIGH=ON, LOW=OFF |
| Switch 3 | GPIO 18  | 192.168.1.5 | HIGH=ON, LOW=OFF |
| Switch 4 | GPIO 19  | 192.168.1.6 | HIGH=ON, LOW=OFF |
| Switch 5 | GPIO 21  | 192.168.1.3 | HIGH=ON, LOW=OFF |

**Note**: Switch 1 uses different logic than switches 2-5 due to wiring differences. See [Wiring Guide](WIRING.md) for detailed connection instructions.

### Configuration

Before building, you need to configure:

1. **WiFi Credentials**: See [WiFi Configuration Setup](#wifi-configuration-setup) above
2. **Bulb IP Addresses**: Update the `switches` array in `main/main.c` (lines 52-57) with your bulb IP addresses

To find your WiZ bulb IP addresses:

- Check your router's DHCP client list
- Use the WiZ app to view bulb details
- Use a network scanner tool
- Broadcast discovery: `192.168.1.255` (see code comments)

### How It Works

The `app_main()` function initializes the system in the following order:

1. **WiFi Initialization**: Connects to WiFi using credentials from `wifi_config.h`
2. **Status LED Setup**: Initializes GPIO 2 as output for status indication
3. **WiFi Connection Wait**: Waits up to 15 seconds for WiFi connection
4. **UDP Socket Initialization**: Sets up UDP socket for WiZ bulb communication
5. **Toggle Handler Task**: Creates a task to handle switch state changes
6. **Toggle Switch GPIO Setup**: Configures all 5 GPIO pins with pull-up resistors and interrupt handlers
7. **Initial State Sync**: Reads initial switch positions and syncs bulb states
8. **Ready State**: System ready for operation

**Switch Operation**:

- **Switch 1**: When toggle is ON (closed/LOW), bulbs 2 & 7 turn ON. When OFF (open/HIGH), bulbs turn OFF.
- **Switches 2-5**: When toggle is ON (closed/LOW), bulbs turn OFF. When OFF (open/HIGH), bulbs turn ON.
- Switch changes are detected via hybrid polling (100ms) + interrupt system
- Changes are debounced (50ms) to prevent false triggers
- Commands are retried up to 3 times if they fail
- Every 2 seconds, the system automatically syncs bulb states with switch positions

**Status LED Feedback**:
- 1 blink = Command sent successfully
- 2 blinks = Error sending command (after retries)
- 3 blinks = WiFi not connected

**Reliability Features**:

- **Polling**: Primary detection method - polls GPIO every 100ms to catch all changes
- **Interrupts**: Fast path for immediate response when switches change
- **Debouncing**: Multiple readings with majority vote to filter noise
- **Retry Logic**: Failed commands automatically retry with 200ms delays
- **Periodic Sync**: Ensures bulbs stay in sync even if commands are missed

**Serial Monitor Output**:

- WiFi connection status and IP address
- Switch initialization and configuration
- Switch change events with GPIO levels
- Bulb control commands sent
- Success/failure status for each command
- Periodic sync operations (when corrections are needed)

## Example folder contents

The project **sample_project** contains one source file in C language [main.c](main/main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt`
files that provide set of directives and instructions describing the project's source files and targets
(executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   ├── main.c                   Main application code
│   ├── wifi_config.h.example    WiFi configuration template
│   └── wifi_config.h            Your WiFi credentials (gitignored)
├── WIRING.md                    Detailed wiring guide for switches
└── README.md                    This is the file you are currently reading
```

### Key Files

- **main/main.c**: Main application code containing:
  - WiFi initialization and connection handling
  - UDP socket management for WiZ bulb communication
  - Switch GPIO configuration and interrupt handlers
  - Switch state polling and change detection
  - Bulb control logic with retry mechanism
  - Periodic sync functionality

- **main/wifi_config.h**: Your WiFi credentials (not in git)
- **WIRING.md**: Complete wiring instructions for connecting 5 toggle switches

Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system.
They are not used or needed when building with CMake and idf.py.
