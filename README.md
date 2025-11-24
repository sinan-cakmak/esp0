# _Sample project_

(See the README.md file in the upper level 'examples' directory for more information about examples.)

This is the simplest buildable example. The example is used by command `idf.py create-project`
that copies the project to user specified path and set it's name. For more information follow the [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project)

## How to use example

We encourage the users to use the example as a template for the new projects.
A recommended way is to follow the instructions on a [docs page](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#start-a-new-project).

## Sample Setup

This example demonstrates a simple LED blinking application. The code in [main.c](main/main.c) implements the following functionality:

- **GPIO Configuration**: Configures GPIO pin 2 (`BLINK_GPIO`) as an output pin
- **LED Blinking**: Continuously toggles the LED on and off with a 500ms delay between states
- **Logging**: Outputs "LED ON" and "LED OFF" messages to the serial console using ESP-IDF logging

### Hardware Requirements

- An ESP32 development board
- An LED connected to GPIO pin 2 (or use the onboard LED if available on your board)
- A current-limiting resistor (typically 220Ω-1kΩ) if using an external LED

### How It Works

The `app_main()` function:

1. Resets GPIO pin 2 to its default state
2. Sets GPIO pin 2 as an output pin
3. Enters an infinite loop that:
   - Sets the GPIO pin HIGH (LED ON) and logs a message
   - Waits 500 milliseconds
   - Sets the GPIO pin LOW (LED OFF) and logs a message
   - Waits 500 milliseconds
   - Repeats

After flashing and running this example, you should see the LED blinking every second, and the serial monitor will display alternating "LED ON" and "LED OFF" messages.

## Example folder contents

The project **sample_project** contains one source file in C language [main.c](main/main.c). The file is located in folder [main](main).

ESP-IDF projects are built using CMake. The project build configuration is contained in `CMakeLists.txt`
files that provide set of directives and instructions describing the project's source files and targets
(executable, library, or both).

Below is short explanation of remaining files in the project folder.

```
├── CMakeLists.txt
├── main
│   ├── CMakeLists.txt
│   └── main.c
└── README.md                  This is the file you are currently reading
```

Additionally, the sample project contains Makefile and component.mk files, used for the legacy Make based build system.
They are not used or needed when building with CMake and idf.py.
