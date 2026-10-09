#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include "cJSON.h"
#include "wiz_protocol.h"

static bool normalize_mac(const char *mac, char normalized[13])
{
    if (!mac) {
        return false;
    }
    size_t length = 0;
    for (; *mac; ++mac) {
        unsigned char c = (unsigned char)*mac;
        if (c == ':' || c == '-') {
            continue;
        }
        if (!isxdigit(c) || length == 12) {
            return false;
        }
        normalized[length++] = (char)tolower(c);
    }
    normalized[length] = '\0';
    return length == 12;
}

bool wiz_mac_equal(const char *left, const char *right)
{
    char a[13], b[13];
    return normalize_mac(left, a) && normalize_mac(right, b) && strcmp(a, b) == 0;
}

bool wiz_parse_pilot(const char *json, const char *expected_mac, bool *state)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(result, "mac");
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(result, "state");
    bool valid = !cJSON_GetObjectItemCaseSensitive(root, "error") &&
                 cJSON_IsString(method) && strcmp(method->valuestring, "getPilot") == 0 &&
                 cJSON_IsString(mac) && wiz_mac_equal(mac->valuestring, expected_mac) &&
                 cJSON_IsBool(value) && state;
    if (valid) {
        *state = cJSON_IsTrue(value);
    }
    cJSON_Delete(root);
    return valid;
}

bool wiz_parse_set_ack(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        return false;
    }
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(result, "success");
    bool valid = !cJSON_GetObjectItemCaseSensitive(root, "error") &&
                 cJSON_IsString(method) && strcmp(method->valuestring, "setPilot") == 0 &&
                 cJSON_IsTrue(success);
    cJSON_Delete(root);
    return valid;
}
