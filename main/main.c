#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define BLINK_GPIO 2 

void app_main(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    while (1) {
        gpio_set_level(BLINK_GPIO, 1);
        ESP_LOGI("BLINK", "LED ON");
        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_set_level(BLINK_GPIO, 0);
        ESP_LOGI("BLINK", "LED OFF");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}