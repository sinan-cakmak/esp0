#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "controller_config.h"
#include "controller_logic.h"
#include "wiz_protocol.h"

static void test_debounce(void)
{
    switch_debounce_t input = {0};
    // No commandable state until the first input has settled for 50 ms.
    assert(!switch_debounce_sample(&input, 1, 0, 50));
    assert(!input.valid);
    assert(!switch_debounce_sample(&input, 1, 49, 50));
    assert(switch_debounce_sample(&input, 1, 50, 50));
    assert(input.valid && input.stable == 1);
    assert(!switch_debounce_sample(&input, 1, 60, 50));

    // Bounce must restart the settling period; it cannot change the stable state.
    assert(!switch_debounce_sample(&input, 0, 100, 50));
    assert(!switch_debounce_sample(&input, 1, 110, 50));
    assert(!switch_debounce_sample(&input, 0, 120, 50));
    assert(!switch_debounce_sample(&input, 0, 169, 50));
    assert(input.stable == 1);
    assert(switch_debounce_sample(&input, 0, 170, 50));
    assert(input.stable == 0);

    // One switch's bounce cannot affect another switch's debounce.
    switch_debounce_t other = {0};
    assert(!switch_debounce_sample(&other, 1, 180, 50));
    assert(!switch_debounce_sample(&input, 1, 190, 50));
    assert(!switch_debounce_sample(&input, 0, 200, 50));
    assert(switch_debounce_sample(&other, 1, 230, 50));
    assert(input.stable == 0 && other.stable == 1);

    // Device uptime eventually wraps: the stable interval still works.
    switch_debounce_t wrapped = {0};
    assert(!switch_debounce_sample(&wrapped, 0, UINT32_MAX - 20, 50));
    assert(!switch_debounce_sample(&wrapped, 0, 28, 50));
    assert(switch_debounce_sample(&wrapped, 0, 29, 50));

    // On-device timing is in 10 ms ticks, so 50 ms means five ticks.
    switch_debounce_t ticks = {0};
    assert(!switch_debounce_sample(&ticks, 0, 0, 5));
    assert(!switch_debounce_sample(&ticks, 0, 4, 5));
    assert(switch_debounce_sample(&ticks, 0, 5, 5));
}

static void test_protocol(void)
{
    assert(wiz_mac_equal("D8:A0:11:62:BC:9E", "d8a01162bc9e"));
    assert(!wiz_mac_equal("d8a01162bc9e", "d8a01162ba16"));
    assert(!wiz_mac_equal("invalid", "invalid"));
    assert(!wiz_mac_equal("d8a01162bc9e00", "d8a01162bc9e00"));
    assert(!wiz_mac_equal(NULL, "d8a01162bc9e"));
    bool state = false;
    assert(wiz_parse_pilot("{\"method\":\"getPilot\",\"result\":{\"mac\":\"D8A01162BC9E\",\"state\":true}}", "d8a01162bc9e", &state));
    assert(state);
    assert(wiz_parse_pilot("{\"method\":\"getPilot\",\"result\":{\"mac\":\"d8a01162bc9e\",\"state\":false}}", "d8a01162bc9e", &state));
    assert(!state);
    // A reply from another bulb, an acknowledgement, or malformed data is not a state.
    assert(!wiz_parse_pilot("{\"method\":\"getPilot\",\"result\":{\"mac\":\"d8a01162ba16\",\"state\":true}}", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{\"method\":\"syncPilot\",\"result\":{\"mac\":\"d8a01162bc9e\",\"state\":true}}", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{\"method\":\"getPilot\",\"result\":{\"mac\":\"d8a01162bc9e\",\"state\":1}}", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{\"method\":\"getPilot\",\"result\":{\"state\":true}}", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{\"method\":\"getPilot\",\"error\":{},\"result\":{\"mac\":\"d8a01162bc9e\",\"state\":true}}", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{broken", "d8a01162bc9e", &state));
    assert(!wiz_parse_pilot("{}", "d8a01162bc9e", &state));
    assert(wiz_parse_set_ack("{\"method\":\"setPilot\",\"result\":{\"success\":true}}"));
    assert(!wiz_parse_set_ack("{\"method\":\"setPilot\",\"result\":{\"success\":false}}"));
    assert(!wiz_parse_set_ack("{\"method\":\"getPilot\",\"result\":{\"success\":true}}"));
    assert(!wiz_parse_set_ack("{\"method\":\"setPilot\",\"result\":{\"success\":1}}"));
    assert(!wiz_parse_set_ack("{\"method\":\"setPilot\",\"error\":{},\"result\":{\"success\":true}}"));
    assert(!wiz_parse_set_ack("[]"));
}

static void test_assignments(void)
{
    assert(switch_desired_state(0, false));
    assert(!switch_desired_state(1, false));
    assert(!switch_desired_state(0, true));
    assert(switch_desired_state(1, true));
    int bulbs = 0;
    for (int i = 0; i < NUM_SWITCHES; ++i) {
        const switch_assignment_t *sw = &SWITCH_ASSIGNMENTS[i];
        assert(sw->num_bulbs > 0 && sw->num_bulbs <= MAX_BULBS_PER_SWITCH);
        bulbs += sw->num_bulbs;
        for (int b = 0; b < sw->num_bulbs; ++b) {
            assert(wiz_mac_equal(sw->bulb_macs[b], sw->bulb_macs[b]));
            for (int j = 0; j <= i; ++j) {
                int limit = j == i ? b : SWITCH_ASSIGNMENTS[j].num_bulbs;
                for (int k = 0; k < limit; ++k) {
                    assert(!wiz_mac_equal(sw->bulb_macs[b], SWITCH_ASSIGNMENTS[j].bulb_macs[k]));
                }
            }
        }
        for (int j = 0; j < i; ++j) {
            assert(sw->gpio_pin != SWITCH_ASSIGNMENTS[j].gpio_pin);
        }
    }
    assert(bulbs == 6);
}

int main(void)
{
    test_debounce();
    test_protocol();
    test_assignments();
    puts("PASS: debounce, switch isolation/polarity, MAC identity, response validation, configuration");
    return 0;
}
