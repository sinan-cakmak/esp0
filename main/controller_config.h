#ifndef CONTROLLER_CONFIG_H
#define CONTROLLER_CONFIG_H

#include <stdbool.h>

#define NUM_SWITCHES 5
#define MAX_BULBS_PER_SWITCH 2
#define WIZ_PORT 38899
#define LED_STATUS_GPIO 2
#define SWITCH_SAMPLE_MS 10
#define SWITCH_DEBOUNCE_MS 50
#define BULB_SYNC_MS 2000
#define BULB_DISCOVERY_MS 30000

typedef struct {
    int gpio_pin;
    const char *bulb_macs[MAX_BULBS_PER_SWITCH];
    int num_bulbs;
    bool on_when_high;
} switch_assignment_t;

// Preserved from the existing firmware. Physical room labels are not yet verified.
// GPIO5 is a strapping pin: review before soldering; GPIO22 is a possible alternative.
static const switch_assignment_t SWITCH_ASSIGNMENTS[NUM_SWITCHES] = {
    {4,  {"444f8e26e756", "444f8e26e796"}, 2, false},
    {5,  {"d8a01162bc9e"},                 1, true},
    {18, {"d8a01162ba16"},                 1, true},
    {19, {"444f8e308782"},                 1, true},
    {21, {"d8a01170b374"},                 1, true},
};

#endif
