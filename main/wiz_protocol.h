#ifndef WIZ_PROTOCOL_H
#define WIZ_PROTOCOL_H

#include <stdbool.h>

bool wiz_mac_equal(const char *left, const char *right);
bool wiz_parse_pilot(const char *json, const char *expected_mac, bool *state);
bool wiz_parse_set_ack(const char *json);

#endif
