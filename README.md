# _Sample project_

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This is the simplest buildable example. The example is used by command `idf.py create-project`
that copies the project to user specified path and set it's name. For more information follow the [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)

## How to use example

We encourage the users to use the example as a template for the new projects.
A recommended way is to follow the instructions on a [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project).

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

This project is a **WiZ Smart Bulb Controller** for ESP32. It connects to your WiFi network and allows you to control a WiZ smart bulb by pressing the BOOT button on the ESP32 board.

### Features

- **WiFi Connectivity**: Connects to your local WiFi network using credentials from `wifi_config.h`
- **WiZ Bulb Control**: Communicates with WiZ smart bulbs via UDP protocol (port 38899)
- **Button Interface**: Uses the ESP32 BOOT button (GPIO 0) to toggle the bulb on/off
- **Status LED**: Visual feedback via status LED on GPIO 2
- **Automatic Reconnection**: Automatically reconnects to WiFi if connection is lost
- **Bulb Discovery**: Tests bulb communication on startup

### Hardware Requirements

- An ESP32 development board (with BOOT button)
- A WiZ smart bulb connected to the same WiFi network
- Optional: Status LED on GPIO 2 (many ESP32 boards have a built-in LED)

### Configuration

Before building, you need to configure:

1. **WiFi Credentials**: See [WiFi Configuration Setup](#wifi-configuration-setup) above
2. **Bulb IP Address**: Update `WIZ_BULB_IP` in `main/main.c` (line 17) with your bulb's IP address

To find your WiZ bulb's IP address:

- Check your router's DHCP client list
- Use the WiZ app to view bulb details
- Use a network scanner tool

### How It Works

The `app_main()` function initializes the system in the following order:

1. **WiFi Initialization**: Connects to WiFi using credentials from `wifi_config.h`
2. **Status LED Setup**: Initializes GPIO 2 as output for status indication
3. **WiFi Connection Wait**: Waits up to 15 seconds for WiFi connection
4. **UDP Socket Initialization**: Sets up UDP socket for WiZ bulb communication
5. **Bulb Communication Test**: Tests connectivity with the configured bulb IP address
6. **Button Handler Task**: Creates a task to handle button press events
7. **Button GPIO Setup**: Configures GPIO 0 (BOOT button) with interrupt handler
8. **Ready State**: Blinks LED twice to indicate system is ready

**Button Operation**:

- Press the BOOT button to toggle the bulb on/off
- Button presses are debounced (300ms debounce time)
- Status LED provides visual feedback:
  - 1 blink = Command sent successfully
  - 2 blinks = Error sending command
  - 3 blinks = WiFi not connected

**Serial Monitor Output**:

- WiFi connection status and IP address
- Bulb communication test results
- Button press events and bulb state changes
- Error messages if communication fails

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
│   ├── main.c
│   ├── wifi_config.h.example    WiFi configuration template
│   └── wifi_config.h            Your WiFi credentials (gitignored)
└── README.md                    This is the file you are currently reading
```

Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system.
They are not used or needed when building with CMake and idf.py.
