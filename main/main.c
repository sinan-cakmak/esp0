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

// WiZ Bulb Configuration - UPDATE THIS with your bulb's IP address
#define WIZ_BULB_IP    "192.168.1.2"  // TODO: Replace with your bulb's IP address
#define WIZ_PORT       38899

// Toggle Switch GPIO Configuration - Toggle switch between GPIO 4 and GND
#define TOGGLE_GPIO    4  // Physical toggle switch

// Status LED GPIO
#define LED_STATUS_GPIO  2

static const char *TAG = "wifi";
static const char *WIZ_TAG = "wiz";

static int udp_socket = -1;
static bool wifi_connected = false;
static bool bulb_state = false;
static TaskHandle_t button_task_handle = NULL;
static bool sync_in_progress = false;  // Prevent concurrent sync operations

// Forward declarations
esp_err_t wiz_udp_init(void);
esp_err_t wiz_send_command(const char *bulb_ip, const char *json_command);
esp_err_t wiz_receive_response(char *buffer, size_t buffer_size);
esp_err_t wiz_get_pilot(const char *bulb_ip, char *response_buffer, size_t buffer_size);
esp_err_t wiz_set_state(const char *bulb_ip, bool on);
esp_err_t wiz_discover_and_test(const char *bulb_ip);
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
            bulb_state = on;
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

// ========== Button GPIO Functions ==========

/**
 * IRAM_ATTR ISR handler for toggle switch interrupt
 */
static void IRAM_ATTR toggle_isr_handler(void* arg)
{
    if (button_task_handle == NULL) {
        return; // Task not ready yet
    }
    
    // Notify the toggle handler task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(button_task_handle, 1, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * Read toggle switch state with debouncing (multiple reads)
 */
static int read_toggle_state_debounced(void)
{
    int readings[5];
    int high_count = 0;
    
    // Take 5 readings with small delays
    for (int i = 0; i < 5; i++) {
        readings[i] = gpio_get_level(TOGGLE_GPIO);
        if (readings[i] == 1) high_count++;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    
    // Return majority state (HIGH if 3+ readings are HIGH)
    return (high_count >= 3) ? 1 : 0;
}

/**
 * Initialize GPIO pin for toggle switch
 * Toggle switch connected between GPIO 4 and GND:
 * - When switch is ON (closed): GPIO reads LOW (0) -> Bulb OFF (inverted)
 * - When switch is OFF (open): GPIO reads HIGH (1) -> Bulb ON (inverted)
 * Note: Logic is inverted because user reported toggle direction was wrong
 */
void toggle_gpio_init(void)
{
    // Reset GPIO pin
    gpio_reset_pin(TOGGLE_GPIO);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,  // Interrupt on both edges (toggle position change)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << TOGGLE_GPIO),
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
    
    gpio_isr_handler_add(TOGGLE_GPIO, toggle_isr_handler, NULL);

    // Read initial state with debouncing
    int level = read_toggle_state_debounced();
    ESP_LOGI(WIZ_TAG, "Toggle switch GPIO %d initialized, current level: %d", TOGGLE_GPIO, level);
    ESP_LOGI(WIZ_TAG, "Toggle ON (HIGH/1) = Bulb ON, Toggle OFF (LOW/0) = Bulb OFF");
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
 * Sync bulb state with toggle switch state
 * Returns true if sync was successful or not needed
 */
static bool sync_bulb_with_toggle(void)
{
    if (sync_in_progress || !wifi_connected) {
        return false;
    }
    
    sync_in_progress = true;
    
    // Read toggle state with debouncing
    int toggle_state = read_toggle_state_debounced();
    
    // INVERTED LOGIC: HIGH (1) = toggle ON = bulb ON, LOW (0) = toggle OFF = bulb OFF
    bool desired_bulb_state = (toggle_state == 1);
    
    // Only sync if bulb state doesn't match toggle state
    if (bulb_state != desired_bulb_state) {
        ESP_LOGI(WIZ_TAG, "Syncing: Toggle=%d (desired bulb=%s), Current bulb=%s", 
                 toggle_state, desired_bulb_state ? "ON" : "OFF", bulb_state ? "ON" : "OFF");
        
        esp_err_t ret = wiz_set_state(WIZ_BULB_IP, desired_bulb_state);
        sync_in_progress = false;
        return (ret == ESP_OK);
    }
    
    sync_in_progress = false;
    return true;
}

/**
 * Toggle switch handler task - processes toggle position changes
 */
void button_handler_task(void *pvParameters)
{
    uint32_t notification_value;
    uint32_t last_change_time = 0;
    const uint32_t DEBOUNCE_MS = 100;  // Increased debounce for toggle switch
    int last_toggle_state = -1;  // Track previous state
    
    ESP_LOGI(WIZ_TAG, "Toggle switch handler task started");
    
    // Wait for WiFi before reading initial state
    int wifi_wait = 0;
    while (!wifi_connected && wifi_wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wifi_wait++;
    }
    
    if (!wifi_connected) {
        ESP_LOGW(WIZ_TAG, "WiFi not connected, toggle handler will wait");
    }
    
    // Read initial toggle state with debouncing
    last_toggle_state = read_toggle_state_debounced();
    // INVERTED: HIGH (1) = toggle ON = bulb ON
    bulb_state = (last_toggle_state == 1);
    ESP_LOGI(WIZ_TAG, "Initial toggle state: %d (bulb should be %s)", 
             last_toggle_state, bulb_state ? "ON" : "OFF");
    
    // Sync initial state
    if (wifi_connected) {
        sync_bulb_with_toggle();
    }
    
    while (1) {
        // Wait for notification from ISR (toggle position changed) with timeout for periodic sync
        if (xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, pdMS_TO_TICKS(2000))) {
            uint32_t now = xTaskGetTickCount();
            
            // Debounce
            if (now - last_change_time < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                continue;
            }
            last_change_time = now;
            
            // Read current toggle state with debouncing
            int current_toggle_state = read_toggle_state_debounced();
            
            // Only process if state actually changed
            if (current_toggle_state == last_toggle_state) {
                continue;
            }
            
            last_toggle_state = current_toggle_state;
            
            if (!wifi_connected) {
                ESP_LOGW(WIZ_TAG, "WiFi not connected, cannot control bulb");
                led_status_blink(3, 100);
                continue;
            }
            
            // INVERTED LOGIC: HIGH (1) = toggle ON = bulb ON, LOW (0) = toggle OFF = bulb OFF
            bool new_bulb_state = (current_toggle_state == 1);
            
            ESP_LOGI(WIZ_TAG, "*** TOGGLE CHANGED ***");
            ESP_LOGI(WIZ_TAG, "Toggle position: %s (GPIO level: %d)", 
                     current_toggle_state == 1 ? "ON" : "OFF", current_toggle_state);
            ESP_LOGI(WIZ_TAG, "Setting bulb %s", new_bulb_state ? "ON" : "OFF");
            
            esp_err_t ret = wiz_set_state(WIZ_BULB_IP, new_bulb_state);
            if (ret == ESP_OK) {
                ESP_LOGI(WIZ_TAG, "Bulb command sent successfully");
                led_status_blink(1, 100); // Quick blink for feedback
            } else {
                ESP_LOGE(WIZ_TAG, "Failed to send bulb command");
                led_status_blink(2, 200); // Error indication
            }
        } else {
            // Timeout - periodic sync check
            if (wifi_connected) {
                sync_bulb_with_toggle();
            }
        }
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
    
    // Test bulb communication
    ESP_LOGI(WIZ_TAG, "");
    ESP_LOGI(WIZ_TAG, "Testing bulb communication...");
    wiz_discover_and_test(WIZ_BULB_IP);
    ESP_LOGI(WIZ_TAG, "");
    
    // Create toggle handler task FIRST (before initializing GPIO)
    xTaskCreate(button_handler_task, "toggle_handler", 4096, NULL, 10, &button_task_handle);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to start
    
    // Initialize toggle switch GPIO (after task is created)
    toggle_gpio_init();
    
    ESP_LOGI(WIZ_TAG, "========================================");
    ESP_LOGI(WIZ_TAG, "System ready!");
    ESP_LOGI(WIZ_TAG, "Toggle switch on GPIO %d controls bulb", TOGGLE_GPIO);
    ESP_LOGI(WIZ_TAG, "Bulb IP: %s", WIZ_BULB_IP);
    ESP_LOGI(WIZ_TAG, "========================================");
    
    // Blink LED to indicate ready
    led_status_blink(2, 200);
    
    // Main loop - keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
