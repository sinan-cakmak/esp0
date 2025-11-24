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

// Button GPIO Configuration - Using BOOT button on ESP32
#define BUTTON_GPIO    0  // BOOT button

// Status LED GPIO
#define LED_STATUS_GPIO  2

static const char *TAG = "wifi";
static const char *WIZ_TAG = "wiz";

static int udp_socket = -1;
static bool wifi_connected = false;
static bool bulb_state = false;
static TaskHandle_t button_task_handle = NULL;

// Forward declarations
esp_err_t wiz_udp_init(void);
esp_err_t wiz_send_command(const char *bulb_ip, const char *json_command);
esp_err_t wiz_receive_response(char *buffer, size_t buffer_size);
esp_err_t wiz_get_pilot(const char *bulb_ip, char *response_buffer, size_t buffer_size);
esp_err_t wiz_set_state(const char *bulb_ip, bool on);
esp_err_t wiz_discover_and_test(const char *bulb_ip);
void button_gpio_init(void);
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
 * Turn WiZ bulb on or off
 */
esp_err_t wiz_set_state(const char *bulb_ip, bool on)
{
    char json_cmd[128];
    snprintf(json_cmd, sizeof(json_cmd),
             "{\"method\":\"setPilot\",\"params\":{\"state\":%s}}",
             on ? "true" : "false");
    
    esp_err_t ret = wiz_send_command(bulb_ip, json_cmd);
    if (ret == ESP_OK) {
        bulb_state = on;
    }
    return ret;
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
 * IRAM_ATTR ISR handler for button interrupt
 */
static void IRAM_ATTR button_isr_handler(void* arg)
{
    if (button_task_handle == NULL) {
        return; // Task not ready yet
    }
    
    // Notify the button handler task
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(button_task_handle, 1, eSetBits, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * Initialize GPIO pin for BOOT button
 */
void button_gpio_init(void)
{
    // First, check current GPIO state
    gpio_reset_pin(BUTTON_GPIO);
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,  // Interrupt on falling edge (button press)
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io_conf);

    // Install ISR service if not already installed
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        gpio_install_isr_service(0);
        isr_service_installed = true;
    }
    
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);

    // Read initial state
    int level = gpio_get_level(BUTTON_GPIO);
    ESP_LOGI(WIZ_TAG, "Button GPIO %d (BOOT button) initialized, current level: %d", BUTTON_GPIO, level);
    ESP_LOGI(WIZ_TAG, "Button should be HIGH (1) when not pressed, LOW (0) when pressed");
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
 * Button handler task - processes button presses
 */
void button_handler_task(void *pvParameters)
{
    uint32_t notification_value;
    uint32_t last_press_time = 0;
    const uint32_t DEBOUNCE_MS = 300;
    
    ESP_LOGI(WIZ_TAG, "Button handler task started");
    
    while (1) {
        // Wait for notification from ISR
        if (xTaskNotifyWait(0x00, ULONG_MAX, &notification_value, portMAX_DELAY)) {
            uint32_t now = xTaskGetTickCount();
            
            // Debounce
            if (now - last_press_time < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                continue;
            }
            last_press_time = now;
            
            if (!wifi_connected) {
                ESP_LOGW(WIZ_TAG, "WiFi not connected, cannot control bulb");
                led_status_blink(3, 100);
                continue;
            }
            
            ESP_LOGI(WIZ_TAG, "*** BUTTON PRESSED ***");
            
            // Toggle bulb state
            bulb_state = !bulb_state;
            ESP_LOGI(WIZ_TAG, "Toggling bulb %s", bulb_state ? "ON" : "OFF");
            
            esp_err_t ret = wiz_set_state(WIZ_BULB_IP, bulb_state);
            if (ret == ESP_OK) {
                ESP_LOGI(WIZ_TAG, "Bulb command sent successfully");
                led_status_blink(1, 100); // Quick blink for feedback
            } else {
                ESP_LOGE(WIZ_TAG, "Failed to send bulb command");
                led_status_blink(2, 200); // Error indication
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
    
    // Create button handler task FIRST (before initializing GPIO)
    xTaskCreate(button_handler_task, "button_handler", 4096, NULL, 10, &button_task_handle);
    vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to start
    
    // Initialize button GPIO (after task is created)
    button_gpio_init();
    
    ESP_LOGI(WIZ_TAG, "========================================");
    ESP_LOGI(WIZ_TAG, "System ready!");
    ESP_LOGI(WIZ_TAG, "Press BOOT button (GPIO %d) to toggle bulb", BUTTON_GPIO);
    ESP_LOGI(WIZ_TAG, "Bulb IP: %s", WIZ_BULB_IP);
    ESP_LOGI(WIZ_TAG, "========================================");
    
    // Blink LED to indicate ready
    led_status_blink(2, 200);
    
    // Main loop - keep task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
