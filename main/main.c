#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "controller_config.h"
#include "controller_logic.h"
#include "wiz_protocol.h"

#ifndef CONFIG_ESP0_INPUT_TEST_ONLY
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "wifi_config.h"
#endif

static const char *TAG = "controller";
static portMUX_TYPE input_lock = portMUX_INITIALIZER_UNLOCKED;
static int stable_levels[NUM_SWITCHES];
static bool input_valid[NUM_SWITCHES];
static TaskHandle_t bulb_task_handle;

#ifndef CONFIG_ESP0_INPUT_TEST_ONLY
static bool desired_state(int index, bool *state)
{
    portENTER_CRITICAL(&input_lock);
    bool valid = input_valid[index];
    int level = stable_levels[index];
    portEXIT_CRITICAL(&input_lock);
    *state = switch_desired_state(level, SWITCH_ASSIGNMENTS[index].on_when_high);
    return valid;
}
#endif

static void validate_configuration(void)
{
    for (int i = 0; i < NUM_SWITCHES; ++i) {
        const switch_assignment_t *sw = &SWITCH_ASSIGNMENTS[i];
        // Output-capable inputs have internal pull-ups on the classic ESP32.
        ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(sw->gpio_pin) &&
                        sw->gpio_pin != LED_STATUS_GPIO &&
                        sw->num_bulbs > 0 && sw->num_bulbs <= MAX_BULBS_PER_SWITCH
                        ? ESP_OK : ESP_ERR_INVALID_ARG);
        for (int j = 0; j < i; ++j) {
            ESP_ERROR_CHECK(sw->gpio_pin != SWITCH_ASSIGNMENTS[j].gpio_pin
                            ? ESP_OK : ESP_ERR_INVALID_ARG);
        }
        for (int b = 0; b < sw->num_bulbs; ++b) {
            ESP_ERROR_CHECK(wiz_mac_equal(sw->bulb_macs[b], sw->bulb_macs[b])
                            ? ESP_OK : ESP_ERR_INVALID_ARG);
            for (int j = 0; j <= i; ++j) {
                int limit = j == i ? b : SWITCH_ASSIGNMENTS[j].num_bulbs;
                for (int k = 0; k < limit; ++k) {
                    ESP_ERROR_CHECK(!wiz_mac_equal(sw->bulb_macs[b], SWITCH_ASSIGNMENTS[j].bulb_macs[k])
                                    ? ESP_OK : ESP_ERR_INVALID_ARG);
                }
            }
            ESP_LOGI(TAG, "Mapping: switch %d GPIO %d -> MAC %s (%s=ON)",
                     i + 1, sw->gpio_pin, sw->bulb_macs[b], sw->on_when_high ? "HIGH" : "LOW");
        }
    }
}

static void gpio_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NUM_SWITCHES; ++i) {
        ESP_ERROR_CHECK(gpio_reset_pin(SWITCH_ASSIGNMENTS[i].gpio_pin));
        mask |= 1ULL << SWITCH_ASSIGNMENTS[i].gpio_pin;
    }
    const gpio_config_t config = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_reset_pin(LED_STATUS_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(LED_STATUS_GPIO, 0));
}

// Only this task samples switches. Network timeouts cannot interrupt debouncing.
static void switch_task(void *arg)
{
    switch_debounce_t inputs[NUM_SWITCHES] = {0};
    TickType_t next_sample = xTaskGetTickCount();
#ifdef CONFIG_ESP0_INPUT_TEST_ONLY
    int raw_previous[NUM_SWITCHES] = {-1, -1, -1, -1, -1};
    TickType_t last_report = next_sample;
#endif
    while (true) {
        uint32_t changed = 0;
        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < NUM_SWITCHES; ++i) {
            int level = gpio_get_level(SWITCH_ASSIGNMENTS[i].gpio_pin);
#ifdef CONFIG_ESP0_INPUT_TEST_ONLY
            if (level != raw_previous[i]) {
                ESP_LOGI(TAG, "RAW switch %d GPIO %d level=%d", i + 1, SWITCH_ASSIGNMENTS[i].gpio_pin, level);
                raw_previous[i] = level;
            }
#endif
            if (switch_debounce_sample(&inputs[i], level, now, pdMS_TO_TICKS(SWITCH_DEBOUNCE_MS))) {
                portENTER_CRITICAL(&input_lock);
                stable_levels[i] = inputs[i].stable;
                input_valid[i] = true;
                portEXIT_CRITICAL(&input_lock);
                bool state = switch_desired_state(level, SWITCH_ASSIGNMENTS[i].on_when_high);
                ESP_LOGI(TAG, "STABLE switch %d GPIO %d level=%d desired=%s", i + 1,
                         SWITCH_ASSIGNMENTS[i].gpio_pin, level, state ? "ON" : "OFF");
                changed |= 1UL << i;
            }
        }
        if (changed && bulb_task_handle) {
            xTaskNotify(bulb_task_handle, changed, eSetBits);
        }
#ifdef CONFIG_ESP0_INPUT_TEST_ONLY
        if (now - last_report >= pdMS_TO_TICKS(5000)) {
            ESP_LOGI(TAG, "INPUT snapshot levels=%d,%d,%d,%d,%d valid=%d,%d,%d,%d,%d",
                     inputs[0].stable, inputs[1].stable, inputs[2].stable, inputs[3].stable, inputs[4].stable,
                     inputs[0].valid, inputs[1].valid, inputs[2].valid, inputs[3].valid, inputs[4].valid);
            last_report = now;
        }
#endif
        vTaskDelayUntil(&next_sample, pdMS_TO_TICKS(SWITCH_SAMPLE_MS));
    }
}

#ifndef CONFIG_ESP0_INPUT_TEST_ONLY
#define WIFI_CONNECTED_BIT BIT0
#define NETWORK_CHANGED_BIT BIT1
#define RESPONSE_TIMEOUT_MS 600
#define MAX_RETRIES 3

static EventGroupHandle_t wifi_events;
// Only the bulb task reads/writes addresses or uses UDP sockets.
static char bulb_ips[NUM_SWITCHES][MAX_BULBS_PER_SWITCH][16];

static bool wifi_connected(void)
{
    return (xEventGroupGetBits(wifi_events) & WIFI_CONNECTED_BIT) != 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_ERROR_CHECK(esp_wifi_connect());
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_events, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(wifi_events, NETWORK_CHANGED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected; reconnecting (switch sampling continues)");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT | NETWORK_CHANGED_BIT);
    }
}

static void wifi_init(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    wifi_config_t credentials = {.sta = {.ssid = WIFI_SSID, .password = WIFI_PASSWORD}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &credentials));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static int udp_open(int timeout_ms, bool broadcast)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -1;
    }
    struct timeval timeout = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    int enabled = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        (broadcast && setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled)) < 0)) {
        close(sock);
        return -1;
    }
    return sock;
}

// A fresh socket per request isolates late replies from earlier transactions.
// Source IP/port and JSON method are validated; getPilot also validates the MAC.
static bool wiz_request(const char *ip, const char *command, const char *mac, bool *state)
{
    struct sockaddr_in destination = {.sin_family = AF_INET, .sin_port = htons(WIZ_PORT)};
    if (!wifi_connected() || inet_pton(AF_INET, ip, &destination.sin_addr) != 1 ||
        destination.sin_addr.s_addr == INADDR_ANY || destination.sin_addr.s_addr == INADDR_BROADCAST ||
        IN_MULTICAST(ntohl(destination.sin_addr.s_addr))) {
        return false;
    }
    int sock = udp_open(100, false);
    if (sock < 0) {
        return false;
    }
    bool success = false;
    size_t command_length = strlen(command);
    if (sendto(sock, command, command_length, 0, (struct sockaddr *)&destination, sizeof(destination)) == (int)command_length) {
        TickType_t start = xTaskGetTickCount();
        while (wifi_connected() && xTaskGetTickCount() - start < pdMS_TO_TICKS(RESPONSE_TIMEOUT_MS)) {
            char response[1024];
            struct sockaddr_in source;
            socklen_t source_size = sizeof(source);
            int length = recvfrom(sock, response, sizeof(response) - 1, 0, (struct sockaddr *)&source, &source_size);
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                break;
            }
            if (length == 0 || source.sin_addr.s_addr != destination.sin_addr.s_addr || source.sin_port != htons(WIZ_PORT)) {
                continue;
            }
            response[length] = '\0';
            if (mac ? wiz_parse_pilot(response, mac, state) : wiz_parse_set_ack(response)) {
                success = true;
                break;
            }
        }
    }
    close(sock);
    return success;
}

static bool wiz_get_state(const char *ip, const char *mac, bool *state)
{
    return wiz_request(ip, "{\"method\":\"getPilot\",\"params\":{}}", mac, state);
}

static void discover_bulbs(void)
{
    memset(bulb_ips, 0, sizeof(bulb_ips));
    int sock = udp_open(100, true);
    if (sock < 0) {
        ESP_LOGE(TAG, "Cannot create discovery socket");
        return;
    }
    ESP_LOGI(TAG, "Discovering configured bulbs by MAC...");
    const char *command = "{\"method\":\"getPilot\",\"params\":{}}";
    const struct sockaddr_in destination = {
        .sin_family = AF_INET, .sin_port = htons(WIZ_PORT), .sin_addr.s_addr = INADDR_BROADCAST,
    };
    TickType_t start = xTaskGetTickCount();
    TickType_t last_send = start;
    bool first = true;
    while (wifi_connected() && xTaskGetTickCount() - start < pdMS_TO_TICKS(3000)) {
        TickType_t now = xTaskGetTickCount();
        if (first || now - last_send >= pdMS_TO_TICKS(1000)) {
            if (sendto(sock, command, strlen(command), 0, (const struct sockaddr *)&destination, sizeof(destination)) < 0) {
                ESP_LOGW(TAG, "Discovery broadcast failed: errno %d", errno);
            }
            first = false;
            last_send = now;
        }
        char response[1024];
        struct sockaddr_in source;
        socklen_t source_size = sizeof(source);
        int length = recvfrom(sock, response, sizeof(response) - 1, 0, (struct sockaddr *)&source, &source_size);
        if (length < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            break;
        }
        if (!length || source.sin_port != htons(WIZ_PORT)) {
            continue;
        }
        response[length] = '\0';
        for (int i = 0; i < NUM_SWITCHES; ++i) {
            for (int b = 0; b < SWITCH_ASSIGNMENTS[i].num_bulbs; ++b) {
                bool state;
                if (wiz_parse_pilot(response, SWITCH_ASSIGNMENTS[i].bulb_macs[b], &state)) {
                    char address[16];
                    if (inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address)) && strcmp(bulb_ips[i][b], address) != 0) {
                        snprintf(bulb_ips[i][b], sizeof(bulb_ips[i][b]), "%s", address);
                        ESP_LOGI(TAG, "Found switch %d MAC %s at %s", i + 1, SWITCH_ASSIGNMENTS[i].bulb_macs[b], address);
                    }
                }
            }
        }
    }
    close(sock);
    for (int i = 0; i < NUM_SWITCHES; ++i) {
        for (int b = 0; b < SWITCH_ASSIGNMENTS[i].num_bulbs; ++b) {
            if (!bulb_ips[i][b][0]) {
                ESP_LOGW(TAG, "Missing switch %d MAC %s; no command will be sent to it", i + 1, SWITCH_ASSIGNMENTS[i].bulb_macs[b]);
            }
        }
    }
}

static bool sync_bulb(int switch_index, int bulb_index)
{
    const char *ip = bulb_ips[switch_index][bulb_index];
    const char *mac = SWITCH_ASSIGNMENTS[switch_index].bulb_macs[bulb_index];
    if (!ip[0]) {
        return false;
    }
    for (int attempt = 0; attempt < MAX_RETRIES && wifi_connected(); ++attempt) {
        bool actual, desired;
        // Check identity before writing, in case DHCP has assigned this IP to another device.
        if (wiz_get_state(ip, mac, &actual) && desired_state(switch_index, &desired)) {
            if (actual == desired) {
                return true;
            }
            char command[96];
            snprintf(command, sizeof(command), "{\"method\":\"setPilot\",\"params\":{\"state\":%s}}", desired ? "true" : "false");
            ESP_LOGI(TAG, "Setting switch %d MAC %s -> %s", switch_index + 1, mac, desired ? "ON" : "OFF");
            bool acknowledged = wiz_request(ip, command, NULL, NULL);
            // Read back even if the acknowledgement was lost. Never cache an assumed state.
            if (wiz_get_state(ip, mac, &actual) && actual == desired) {
                bool latest;
                if (desired_state(switch_index, &latest) && latest == desired) {
                    ESP_LOGI(TAG, "Confirmed MAC %s %s%s", mac, desired ? "ON" : "OFF", acknowledged ? "" : " (ack lost; read-back succeeded)");
                    return true;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG, "Unconfirmed switch %d MAC %s; will rediscover and retry", switch_index + 1, mac);
    bulb_ips[switch_index][bulb_index][0] = '\0';
    return false;
}

static void bulb_task(void *arg)
{
    bool network_ready = false;
    bool discovery_needed = true;
    TickType_t last_discovery = 0;
    TickType_t last_sync = 0;
    const uint32_t all_switches = (1UL << NUM_SWITCHES) - 1;
    while (true) {
        EventBits_t events = xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(1000));
        if (!(events & WIFI_CONNECTED_BIT)) {
            gpio_set_level(LED_STATUS_GPIO, 0);
            network_ready = false;
            continue;
        }
        bool refresh = !network_ready || (events & NETWORK_CHANGED_BIT);
        if (refresh) {
            xEventGroupClearBits(wifi_events, NETWORK_CHANGED_BIT);
        }
        bool discovered = refresh || (discovery_needed && xTaskGetTickCount() - last_discovery >= pdMS_TO_TICKS(BULB_DISCOVERY_MS));
        if (discovered) {
            discover_bulbs();
            last_discovery = xTaskGetTickCount();
            network_ready = true;
        }
        uint32_t changed = 0;
        xTaskNotifyWait(0, UINT32_MAX, &changed, pdMS_TO_TICKS(100));
        TickType_t now = xTaskGetTickCount();
        bool full_sync = discovered || now - last_sync >= pdMS_TO_TICKS(BULB_SYNC_MS);
        uint32_t selected = full_sync ? all_switches : changed;
        if (selected) {
            bool all_ok = true;
            for (int i = 0; i < NUM_SWITCHES; ++i) {
                if (!(selected & (1UL << i))) {
                    continue;
                }
                for (int b = 0; b < SWITCH_ASSIGNMENTS[i].num_bulbs; ++b) {
                    if (!sync_bulb(i, b)) {
                        all_ok = false;
                        discovery_needed = true;
                    }
                }
            }
            if (full_sync) {
                last_sync = xTaskGetTickCount();
                discovery_needed = !all_ok;
                gpio_set_level(LED_STATUS_GPIO, all_ok);
            } else if (!all_ok) {
                gpio_set_level(LED_STATUS_GPIO, 0);
            }
        }
    }
}
#endif

void app_main(void)
{
    ESP_LOGI(TAG, "WiZ five-switch controller");
    validate_configuration();
    gpio_init();
#ifdef CONFIG_ESP0_INPUT_TEST_ONLY
    ESP_LOGW(TAG, "INPUT TEST ONLY: WiFi disabled, no bulb commands");
#else
    wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(bulb_task, "bulbs", 8192, NULL, 5, &bulb_task_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#endif
    ESP_ERROR_CHECK(xTaskCreate(switch_task, "switches", 4096, NULL, 10, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#ifndef CONFIG_ESP0_INPUT_TEST_ONLY
    wifi_init();
#endif
}
