#include "controller_logic.h"

bool switch_debounce_sample(switch_debounce_t *input, int level,
                            uint32_t now, uint32_t debounce_time)
{
    if (!input->initialized || level != input->candidate) {
        input->candidate = level;
        input->candidate_since = now;
        input->initialized = true;
        return false;
    }
    if ((!input->valid || input->stable != level) &&
        now - input->candidate_since >= debounce_time) {
        input->stable = level;
        input->valid = true;
        return true;
    }
    return false;
}

bool switch_desired_state(int level, bool on_when_high)
{
    return on_when_high ? level == 1 : level == 0;
}
