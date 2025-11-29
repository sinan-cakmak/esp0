#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "driver/gpio.h"
#include "wifi_config.h"
#include "cJSON.h"

// WiZ Bulb Configuration
#define WIZ_PORT       38899
#define NUM_SWITCHES   5
#define MAX_BULBS_PER_SWITCH 2

// Toggle Switch GPIO Configuration - 5 switches
#define SWITCH_GPIO_1    4   // Switch 1: GPIO 4
#define SWITCH_GPIO_2    5   // Switch 2: GPIO 5
#define SWITCH_GPIO_3    18  // Switch 3: GPIO 18
#define SWITCH_GPIO_4    19  // Switch 4: GPIO 19
#define SWITCH_GPIO_5    21  // Switch 5: GPIO 21

// Status LED GPIO
#define LED_STATUS_GPIO  2

// Switch and Bulb Configuration Structure
typedef struct {
    int gpio_pin;
    char bulb_ips[MAX_BULBS_PER_SWITCH][16];  // Mutable IP strings
    const char* bulb_macs[MAX_BULBS_PER_SWITCH]; // MAC addresses for discovery
    int num_bulbs;
    bool last_state;
    bool bulb_states[MAX_BULBS_PER_SWITCH];
    bool invert_logic;  // true = HIGH=ON LOW=OFF, false = LOW=ON HIGH=OFF
} switch_config_t;

static const char *TAG = "wifi";
static const char *WIZ_TAG = "wiz";

static int udp_socket = -1;
static bool wifi_connected = false;
static TaskHandle_t button_task_handle = NULL;
static bool sync_in_progress = false;  // Prevent concurrent sync operations

// Switch configurations - Switch 1 controls bulbs 2&7 together
// Switch 1: LOW=ON HIGH=OFF (invert_logic=false)
// Switches 2-5: HIGH=ON LOW=OFF (invert_logic=true) - inverted logic
// Note: IPs are discovered via MAC address at startup
static switch_config_t switches[NUM_SWITCHES] = {
    {SWITCH_GPIO_1, {"", ""}, {"444f8e26e756","444f8e26e796"}, 2, -1, {false, false}, false},  // Switch 1: Bulbs 2 & 7 (MACs)
    {SWITCH_GPIO_2, {""}, {"d8a01162bc9e"}, 1, -1, {false}, true},                               // Switch 2: Bulb 4 (MAC)
    {SWITCH_GPIO_3, {""}, {"d8a01162ba16"}, 1, -1, {false}, true},                               // Switch 3: Bulb 5 (MAC)
    {SWITCH_GPIO_4, {""}, {"444f8e308782"}, 1, -1, {false}, true},                               // Switch 4: Bulb 6 (MAC)
    {SWITCH_GPIO_5, {""}, {"d8a01170b374"}, 1, -1, {false}, true}                                // Switch 5: Bulb 3 (MAC)
};

// Forward declarations
esp_err_t wiz_udp_init(void);
esp_err_t wiz_send_command(const char *bulb_ip, const char *json_command);
esp_err_t wiz_receive_response(char *buffer, size_t buffer_size);
esp_err_t wiz_get_pilot(const char *bulb_ip, char *response_buffer, size_t buffer_size);
esp_err_t wiz_set_state(const char *bulb_ip, bool on);
esp_err_t wiz_discover_and_test(const char *bulb_ip);
void wiz_discover_bulbs(void);
void toggle_gpio_init(void);
void led_status_init(void);
void led_status_blink(uint32_t count, uint32_t delay_ms);

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        wifi_connected = false;
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa((ip4_addr_t*)&event->ip_info.ip));
        wifi_connected = true;
        // Initialize UDP socket for WiZ communication
        wiz_udp_init();
    }
}

void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    
    ESP_LOGI(TAG, "Connecting to %s...", WIFI_SSID);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// ========== WiZ Bulb UDP Communication Functions ==========

/**
 * Initialize UDP socket for WiZ bulb communication
 */
esp_err_t wiz_udp_init(void)
{
    if (udp_socket >= 0) {
        close(udp_socket);
    }

    udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket < 0) {
        ESP_LOGE(WIZ_TAG, "Failed to create UDP socket");
        return ESP_FAIL;
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(WIZ_TAG, "UDP socket initialized");
    return ESP_OK;
}

/**
 * Send JSON command to WiZ bulb via UDP
 */
esp_err_t wiz_send_command(const char *bulb_ip, const char *json_command)
{
    if (udp_socket < 0) {
        ESP_LOGE(WIZ_TAG, "UDP socket not initialized");
        return ESP_FAIL;
    }

    if (!wifi_connected) {
        ESP_LOGE(WIZ_TAG, "WiFi not connected");
        return ESP_FAIL;
    }

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(bulb_ip);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(WIZ_PORT);

    int len = strlen(json_command);
    int err = sendto(udp_socket, json_command, len, 0,
                     (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (err < 0) {
        ESP_LOGE(WIZ_TAG, "Error occurred during sending: errno %d", errno);
        return ESP_FAIL;
    }

    ESP_LOGI(WIZ_TAG, "Sent to %s: %s", bulb_ip, json_command);
    return ESP_OK;
}

/**
 * Receive response from WiZ bulb
 */
esp_err_t wiz_receive_response(char *buffer, size_t buffer_size)
{
    if (udp_socket < 0) {
        return ESP_FAIL;
    }

    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(udp_socket, buffer, buffer_size - 1, 0,
                       (struct sockaddr *)&source_addr, &socklen);

    if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGW(WIZ_TAG, "No response received (timeout)");
        } else {
            ESP_LOGE(WIZ_TAG, "recvfrom failed: errno %d", errno);
        }
        return ESP_FAIL;
    }

    buffer[len] = '\0';
    ESP_LOGI(WIZ_TAG, "Received from %s: %s", inet_ntoa(source_addr.sin_addr), buffer);
    return ESP_OK;
}

/**
 * Get current WiZ bulb state (discovery/test function)
 */
esp_err_t wiz_get_pilot(const char *bulb_ip, char *response_buffer, size_t buffer_size)
{
    const char *json_cmd = "{\"method\":\"getPilot\",\"params\":{}}";
    
    esp_err_t ret = wiz_send_command(bulb_ip, json_cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(200)); // Wait for response
    return wiz_receive_response(response_buffer, buffer_size);
}

/**
 * Turn WiZ bulb on or off with retry logic
 */
esp_err_t wiz_set_state(const char *bulb_ip, bool on)
{
    char json_cmd[128];
    snprintf(json_cmd, sizeof(json_cmd),
             "{\"method\":\"setPilot\",\"params\":{\"state\":%s}}",
             on ? "true" : "false");
    
    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 200;
    
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        esp_err_t ret = wiz_send_command(bulb_ip, json_cmd);
        if (ret == ESP_OK) {
            if (attempt > 0) {
                ESP_LOGW(WIZ_TAG, "Bulb command succeeded on attempt %d", attempt + 1);
            }
            return ESP_OK;
        }
        
        if (attempt < MAX_RETRIES - 1) {
            ESP_LOGW(WIZ_TAG, "Bulb command failed, retrying in %dms (attempt %d/%d)", 
                     RETRY_DELAY_MS, attempt + 1, MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
        }
    }
    
    ESP_LOGE(WIZ_TAG, "Failed to set bulb state after %d attempts", MAX_RETRIES);
    return ESP_FAIL;
}

/**
 * Discover and test communication with WiZ bulb
 */
esp_err_t wiz_discover_and_test(const char *bulb_ip)
{
    ESP_LOGI(WIZ_TAG, "========================================");
    ESP_LOGI(WIZ_TAG, "Testing communication with bulb: %s", bulb_ip);
    ESP_LOGI(WIZ_TAG, "========================================");
    
    char response[512];
    esp_err_t ret = wiz_get_pilot(bulb_ip, response, sizeof(response));
    
    if (ret == ESP_OK) {
        ESP_LOGI(WIZ_TAG, "SUCCESS! Bulb responded:");
        ESP_LOGI(WIZ_TAG, "%s", response);
        ESP_LOGI(WIZ_TAG, "Bulb is reachable and responding!");
        return ESP_OK;
    } else {
        ESP_LOGE(WIZ_TAG, "FAILED! Could not communicate with bulb");
        ESP_LOGE(WIZ_TAG, "Check:");
        ESP_LOGE(WIZ_TAG, "  1. Bulb IP address is correct: %s", bulb_ip);
        ESP_LOGE(WIZ_TAG, "  2. Bulb is powered on");
        ESP_LOGE(WIZ_TAG, "  3. ESP32 and bulb are on the same WiFi network");
        return ESP_FAIL;
    }
}

/**
 * Discover WiZ bulbs on the network and update IPs based on MAC addresses
 */
void wiz_discover_bulbs(void)
{
    ESP_LOGI(WIZ_TAG, "Starting WiZ bulb discovery...");
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(WIZ_TAG, "Failed to create discovery socket");
        return;
    }

    // Enable broadcast
    int broadcast = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        ESP_LOGE(WIZ_TAG, "Failed to enable broadcast");
        close(sock);
        return;
    }

    // Set receive timeout
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Send discovery packet
    struct sockaddr_in dest_addr;
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(WIZ_PORT);
    dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    const char *msg = "{\"method\":\"getPilot\",\"params\":{}}";
    int err = sendto(sock, msg, strlen(msg), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(WIZ_TAG, "Failed to send discovery packet");
        close(sock);
        return;
    }

    // Listen for responses
    char rx_buffer[1024];
    struct sockaddr_in source_addr;
    socklen_t socklen = sizeof(source_addr);
    
    uint32_t start_tick = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start_tick) < pdMS_TO_TICKS(3000)) {
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        if (len > 0) {
            rx_buffer[len] = 0;
            
            cJSON *root = cJSON_Parse(rx_buffer);
            if (root) {
                cJSON *result = cJSON_GetObjectItem(root, "result");
                if (result) {
                    cJSON *mac_item = cJSON_GetObjectItem(result, "mac");
                    if (mac_item && cJSON_IsString(mac_item)) {
                        const char *mac = mac_item->valuestring;
                        char *ip_str = inet_ntoa(source_addr.sin_addr);
                        
                        // Check if this MAC matches any of our configured bulbs
                        for (int i = 0; i < NUM_SWITCHES; i++) {
                            for (int j = 0; j < switches[i].num_bulbs; j++) {
                                if (switches[i].bulb_macs[j] && strcmp(mac, switches[i].bulb_macs[j]) == 0) {
                                    ESP_LOGI(WIZ_TAG, "Found configured bulb! MAC: %s, IP: %s (Switch %d)", 
                                             mac, ip_str, i + 1);
                                    strncpy(switches[i].bulb_ips[j], ip_str, sizeof(switches[i].bulb_ips[j]) - 1);
                                }
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        } else {
            break; // Timeout or error
        }
    }

    close(sock);
    ESP_LOGI(WIZ_TAG, "Discovery complete");
}

// ========== Button GPIO Functions ==========

/**
 * IRAM_ATTR ISR handler for toggle switch interrupt
 * arg contains the switch index
 */
static void IRAM_ATTR toggle_isr_handler(void* arg)
{
    if (button_task_handle == NULL) {
        return; // Task not ready yet
    }
    
    // Notify the toggle handler task (pass switch index in notification)
    uint32_t switch_index = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(button_task_handle, (1UL << switch_index), eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * Read toggle switch state with debouncing (multiple reads)
 */
static int read_toggle_state_debounced(int gpio_pin)
{
    int readings[5];
    int high_count = 0;
    
    // Take 5 readings with small delays
    for (int i = 0; i < 5; i++) {
        readings[i] = gpio_get_level(gpio_pin);
        if (readings[i] == 1) high_count++;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    // Return majority state (HIGH if 3+ readings are HIGH)
    return (high_count >= 3) ? 1 : 0;
}

/**
 * Initialize GPIO pins for all toggle switches
 * Toggle switches connected between GPIO pins and GND:
 * - When switch is ON (closed): GPIO reads LOW (0) -> Bulb ON
 * - When switch is OFF (open): GPIO reads HIGH (1) -> Bulb OFF
 */
void toggle_gpio_init(void)
{
    // Build pin mask for all switches
    uint64_t pin_mask = 0;
    
    // Reset all pins FIRST (before configuration)
    for (int i = 0; i < NUM_SWITCHES; i++) {
        gpio_reset_pin(switches[i].gpio_pin);
        pin_mask |= (1ULL << switches[i].gpio_pin);
    }
    
    // Configure all switch GPIOs at once (AFTER reset)
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,  // Interrupt on both edges (toggle position change)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = pin_mask,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Pull-up so open switch reads HIGH
    };
    gpio_config(&io_conf);

    // Install ISR service if not already installed
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }
    
    // Add ISR handler for each switch (pass switch index as argument)
    // NOTE: Do NOT call gpio_reset_pin() here - it would clear the config!
    for (int i = 0; i < NUM_SWITCHES; i++) {
        gpio_isr_handler_add(switches[i].gpio_pin, toggle_isr_handler, (void*)i);
        
        // Read initial state with debouncing
        int level = read_toggle_state_debounced(switches[i].gpio_pin);
        switches[i].last_state = level;
        
        // Set initial bulb state based on switch's invert_logic setting
        // Switch 1: LOW=ON HIGH=OFF (invert_logic=false)
        // Switches 2-5: HIGH=ON LOW=OFF (invert_logic=true)
        bool desired_state = switches[i].invert_logic ? (level == 1) : (level == 0);
        for (int j = 0; j < switches[i].num_bulbs; j++) {
            switches[i].bulb_states[j] = desired_state;
        }
        
        ESP_LOGI(WIZ_TAG, "Switch %d (GPIO %d) initialized, level: %d, bulbs: %d", 
                 i + 1, switches[i].gpio_pin, level, switches[i].num_bulbs);
        for (int j = 0; j < switches[i].num_bulbs; j++) {
            ESP_LOGI(WIZ_TAG, "  -> Bulb %d: %s", j + 1, switches[i].bulb_ips[j]);
        }
    }
    
    ESP_LOGI(WIZ_TAG, "All %d toggle switches initialized", NUM_SWITCHES);
    ESP_LOGI(WIZ_TAG, "Toggle ON (LOW/0) = Bulb ON, Toggle OFF (HIGH/1) = Bulb OFF");
}

/**
 * Initialize status LED GPIO
 */
void led_status_init(void)
{
    gpio_reset_pin(LED_STATUS_GPIO);
    gpio_set_direction(LED_STATUS_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_STATUS_GPIO, 0);
    ESP_LOGI(WIZ_TAG, "Status LED initialized on GPIO %d", LED_STATUS_GPIO);
}

/**
 * Blink status LED
 */
void led_status_blink(uint32_t count, uint32_t delay_ms)
{
    for (uint32_t i = 0; i < count; i++) {
        gpio_set_level(LED_STATUS_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(LED_STATUS_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/**
 * Sync bulbs for a specific switch with its toggle state
 */
static bool sync_switch_bulbs(int switch_idx)
{
    if (switch_idx < 0 || switch_idx >= NUM_SWITCHES || !wifi_connected) {
        return false;
    }
    
    switch_config_t* sw = &switches[switch_idx];
    
    // Read toggle state with debouncing
    int toggle_state = read_toggle_state_debounced(sw->gpio_pin);
    
    // Apply logic based on switch's invert_logic setting
    // Switch 1: LOW=ON HIGH=OFF (invert_logic=false)
    // Switches 2-5: HIGH=ON LOW=OFF (invert_logic=true)
    bool desired_bulb_state = sw->invert_logic ? (toggle_state == 1) : (toggle_state == 0);
    
    bool all_synced = true;
    
    // Sync all bulbs for this switch
    for (int i = 0; i < sw->num_bulbs; i++) {
        if (sw->bulb_states[i] != desired_bulb_state) {
            ESP_LOGI(WIZ_TAG, "Syncing Switch %d: Toggle=%d (bulb=%s), Bulb %s current=%s", 
                     switch_idx + 1, toggle_state, desired_bulb_state ? "ON" : "OFF",
                     sw->bulb_ips[i], sw->bulb_states[i] ? "ON" : "OFF");
            
            esp_err_t ret = wiz_set_state(sw->bulb_ips[i], desired_bulb_state);
            if (ret == ESP_OK) {
                sw->bulb_states[i] = desired_bulb_state;
            } else {
                all_synced = false;
            }
        }
    }
    
    return all_synced;
}

/**
 * Sync all switches with their bulbs
 */
static bool sync_all_switches(void)
{
    if (sync_in_progress || !wifi_connected) {
        return false;
    }
    
    sync_in_progress = true;
    bool all_ok = true;
    
    for (int i = 0; i < NUM_SWITCHES; i++) {
        if (!sync_switch_bulbs(i)) {
            all_ok = false;
        }
    }
    
    sync_in_progress = false;
    return all_ok;
}

/**
 * Toggle switch handler task - processes toggle position changes for all switches
 * Uses polling as primary method with interrupts as fast path
 */
void button_handler_task(void *pvParameters)
{
    uint32_t notification_value;
    uint32_t last_change_times[NUM_SWITCHES] = {0};
    const uint32_t DEBOUNCE_MS = 50;  // Debounce time
    const uint32_t POLL_INTERVAL_MS = 100;  // Poll every 100ms to catch missed interrupts
    const uint32_t SYNC_INTERVAL_MS = 2000;  // Full sync every 2 seconds
    uint32_t last_sync_time = 0;
    
    ESP_LOGI(WIZ_TAG, "Toggle switch handler task started for %d switches", NUM_SWITCHES);
    
    // Wait for WiFi before reading initial state
    int wifi_wait = 0;
    while (!wifi_connected && wifi_wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_wait++;
    }
    
    if (!wifi_connected) {
        ESP_LOGW(WIZ_TAG, "WiFi not connected, toggle handler will wait");
    }
    
    // Sync initial state for all switches
    if (wifi_connected) {
        sync_all_switches();
    }
    
    last_sync_time = xTaskGetTickCount();
    
    while (1) {
        uint32_t now = xTaskGetTickCount();
        
        // Check for interrupt notifications (fast path) - non-blocking check
        // Note: We use polling as primary method, interrupts are just optimization
        xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, 0);
        
        // Process each switch
        for (int i = 0; i < NUM_SWITCHES; i++) {
            switch_config_t* sw = &switches[i];
            
            // Read GPIO state directly (fast, single read for change detection)
            int current_toggle_state = gpio_get_level(sw->gpio_pin);
            
            // Check if state changed
            if (current_toggle_state != sw->last_state) {
                // Debounce check
                if (now - last_change_times[i] < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                    // Too soon after last change - might be noise, skip this switch
                    continue;
                }
                
                // Verify with debounced read before processing
                int debounced_state = read_toggle_state_debounced(sw->gpio_pin);
                
                // Only process if debounced state confirms the change
                if (debounced_state != sw->last_state) {
                    // State has changed and debounce passed - process it
                    last_change_times[i] = now;
                    sw->last_state = debounced_state;
                    current_toggle_state = debounced_state;
                } else {
                    // False alarm, state didn't actually change
                    continue;
                }
                
                if (!wifi_connected) {
                    ESP_LOGW(WIZ_TAG, "WiFi not connected, cannot control bulbs");
                    led_status_blink(3, 100);
                    continue;
                }
                
                // Apply logic based on switch's invert_logic setting
                // Switch 1: LOW=ON HIGH=OFF (invert_logic=false)
                // Switches 2-5: HIGH=ON LOW=OFF (invert_logic=true)
                bool new_bulb_state = sw->invert_logic ? (current_toggle_state == 1) : (current_toggle_state == 0);
                
                ESP_LOGI(WIZ_TAG, "*** SWITCH %d CHANGED ***", i + 1);
                ESP_LOGI(WIZ_TAG, "Switch %d (GPIO %d): %s (level: %d)", 
                         i + 1, sw->gpio_pin, current_toggle_state == 1 ? "ON" : "OFF", current_toggle_state);
                
                // Control all bulbs for this switch
                bool all_success = true;
                for (int j = 0; j < sw->num_bulbs; j++) {
                    ESP_LOGI(WIZ_TAG, "  Setting bulb %s to %s", sw->bulb_ips[j], new_bulb_state ? "ON" : "OFF");
                    
                    esp_err_t ret = wiz_set_state(sw->bulb_ips[j], new_bulb_state);
                    if (ret == ESP_OK) {
                        sw->bulb_states[j] = new_bulb_state;
                    } else {
                        ESP_LOGE(WIZ_TAG, "  Failed to control bulb %s", sw->bulb_ips[j]);
                        all_success = false;
                    }
                }
                
                if (all_success) {
                    ESP_LOGI(WIZ_TAG, "Switch %d: All bulbs updated successfully", i + 1);
                    led_status_blink(1, 100); // Quick blink for feedback
                } else {
                    ESP_LOGE(WIZ_TAG, "Switch %d: Some bulbs failed to update", i + 1);
                    led_status_blink(2, 200); // Error indication
                }
            }
        }
        
        // Periodic sync check (every 2 seconds)
        if (wifi_connected && (now - last_sync_time >= pdMS_TO_TICKS(SYNC_INTERVAL_MS))) {
            sync_all_switches();
            last_sync_time = now;
        }
        
        // Poll interval - check toggle state frequently
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    ESP_LOGI(WIZ_TAG, "========================================");
    ESP_LOGI(WIZ_TAG, "WiZ Bulb Controller - Simple Version");
    ESP_LOGI(WIZ_TAG, "========================================");
    
    // Initialize WiFi
    wifi_init();
    
    // Initialize status LED
    led_status_init();
    
    // Wait for WiFi connection
    ESP_LOGI(WIZ_TAG, "Waiting for WiFi connection...");
    int wait_count = 0;
    while (!wifi_connected && wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_count++;
    }
    
    if (!wifi_connected) {
        ESP_LOGE(WIZ_TAG, "WiFi connection timeout!");
        led_status_blink(5, 200);
        return;
    }
    
    ESP_LOGI(WIZ_TAG, "WiFi connected! Initializing UDP...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Discover bulbs on the network
    wiz_discover_bulbs();
    
    // Create toggle handler task FIRST (before initializing GPIO)
    xTaskCreate(button_handler_task, "toggle_handler", 8192, NULL, 10, &button_task_handle);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to start
    
    // Initialize toggle switch GPIO (after task is created)
    toggle_gpio_init();
    
    ESP_LOGI(WIZ_TAG, "========================================");
    ESP_LOGI(WIZ_TAG, "System ready!");
    ESP_LOGI(WIZ_TAG, "Configured %d switches controlling bulbs:", NUM_SWITCHES);
    for (int i = 0; i < NUM_SWITCHES; i++) {
        ESP_LOGI(WIZ_TAG, "  Switch %d (GPIO %d):", i + 1, switches[i].gpio_pin);
        for (int j = 0; j < switches[i].num_bulbs; j++) {
            ESP_LOGI(WIZ_TAG, "    -> Bulb %s", switches[i].bulb_ips[j]);
        }
    }
    ESP_LOGI(WIZ_TAG, "========================================");
    
    // Blink LED to indicate ready
    led_status_blink(2, 200);
    
    // Main loop - keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
