#include "hc_sr501.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "HC_SR501";

#define HC_SR501_GPIO GPIO_NUM_2

void hc_sr501_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << HC_SR501_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_LOGI(TAG, "HC-SR501 inicializado no GPIO %d", HC_SR501_GPIO);
}

bool hc_sr501_detected(void)
{
    return gpio_get_level(HC_SR501_GPIO) == 1;
}