#ifndef CONTROLLER_LOGIC_H
#define CONTROLLER_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int candidate;
    int stable;
    uint32_t candidate_since;
    bool initialized;
    bool valid;
} switch_debounce_t;

// Times use the same units (FreeRTOS ticks on the device). Unsigned subtraction
// keeps the debounce correct when the clock wraps.
bool switch_debounce_sample(switch_debounce_t *input, int level,
                            uint32_t now, uint32_t debounce_time);
bool switch_desired_state(int level, bool on_when_high);

#endif
