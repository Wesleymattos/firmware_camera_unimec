#include "relay.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "led_status.h"

static const char *TAG = "RELAY";

#define RELAY_GPIO GPIO_NUM_21
#define RELAY_ACTIVE_LEVEL 1

static bool relay_state = false;


void relay_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),

        .mode = GPIO_MODE_OUTPUT,

        .pull_up_en = GPIO_PULLUP_DISABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(&config)
    );

    relay_off();

    ESP_LOGI(
        TAG,
        "Rele inicializado no GPIO %d",
        RELAY_GPIO
    );
}


void relay_on(void)
{
    gpio_set_level(
        RELAY_GPIO,
        RELAY_ACTIVE_LEVEL
    );

    relay_state = true;

    led_status_relay_on();

    ESP_LOGI(
        TAG,
        "Rele LIGADO"
    );
}


void relay_off(void)
{
    gpio_set_level(
        RELAY_GPIO,
        !RELAY_ACTIVE_LEVEL
    );

    relay_state = false;

    led_status_relay_off();

    ESP_LOGI(
        TAG,
        "Rele DESLIGADO"
    );
}