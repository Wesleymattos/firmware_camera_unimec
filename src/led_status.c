#include "led_status.h"

#include "esp_log.h"

#include "led_strip.h"
#include "led_strip_rmt.h"

static const char *TAG = "LED_STATUS";

#define RGB_LED_GPIO 48
#define RGB_LED_COUNT 1

static led_strip_handle_t led_strip = NULL;


/*
 * Define a cor do LED RGB.
 */
static void led_status_set_color(
    uint8_t red,
    uint8_t green,
    uint8_t blue
)
{
    if (led_strip == NULL)
    {
        ESP_LOGE(
            TAG,
            "LED RGB nao inicializado!"
        );

        return;
    }

    ESP_ERROR_CHECK(
        led_strip_set_pixel(
            led_strip,
            0,
            red,
            green,
            blue
        )
    );

    ESP_ERROR_CHECK(
        led_strip_refresh(
            led_strip
        )
    );
}


/*
 * Inicializa o WS2812.
 */
void led_status_init(void)
{
    ESP_LOGI(
        TAG,
        "Inicializando LED RGB..."
    );

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_COUNT,

        .led_model = LED_MODEL_WS2812,

        .color_component_format =
            LED_STRIP_COLOR_COMPONENT_FMT_GRB,

        .flags = {
            .invert_out = false
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,

        .resolution_hz = 10 * 1000 * 1000,

        .mem_block_symbols = 64,

        .flags = {
            .with_dma = false
        }
    };

    ESP_ERROR_CHECK(
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &led_strip
        )
    );

    /*
     * Começa com o LED vermelho,
     * indicando relé desligado.
     */
    led_status_relay_off();

    ESP_LOGI(
        TAG,
        "LED RGB inicializado no GPIO %d",
        RGB_LED_GPIO
    );
}


/*
 * Relé ligado = LED azul.
 */
void led_status_relay_on(void)
{
    led_status_set_color(
        0,
        0,
        80
    );

    ESP_LOGI(
        TAG,
        "LED AZUL - RELE LIGADO"
    );
}


/*
 * Relé desligado = LED vermelho.
 */
void led_status_relay_off(void)
{
    led_status_set_color(
        80,
        0,
        0
    );

    ESP_LOGI(
        TAG,
        "LED VERMELHO - RELE DESLIGADO"
    );
}

/*
 * OTA em andamento = LED amarelo.
 */
void led_status_ota_start(void)
{
    led_status_set_color(
        80,
        50,
        0
    );

    ESP_LOGW(
        TAG,
        "LED AMARELO - ATUALIZACAO OTA EM ANDAMENTO"
    );
}