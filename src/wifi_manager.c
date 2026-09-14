#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "config.h"

static const char *TAG = "WIFI";

/*
 * Bits utilizados para controlar o estado da conexão.
 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t wifi_event_group = NULL;

static int retry_count = 0;

/*
 * Indica se o ESP32 recebeu IP.
 */
static bool wifi_connected = false;

/*
 * Trata os eventos do Wi-Fi e do IP.
 */
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    /*
     * Evento de início do Wi-Fi.
     */
    if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START
    )
    {
        ESP_LOGI(
            TAG,
            "WiFi iniciado. Conectando..."
        );

        esp_wifi_connect();

        return;
    }

    /*
     * Evento de desconexão.
     */
    if (
        event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED
    )
    {
        wifi_connected = false;

        if (retry_count < WIFI_MAX_RETRY)
        {
            retry_count++;

            ESP_LOGW(
                TAG,
                "WiFi desconectado. Tentativa %d/%d",
                retry_count,
                WIFI_MAX_RETRY
            );

            esp_wifi_connect();
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Falha ao conectar ao WiFi."
            );

            xEventGroupSetBits(
                wifi_event_group,
                WIFI_FAIL_BIT
            );
        }

        return;
    }

    /*
     * Evento de obtenção do endereço IP.
     */
    if (
        event_base == IP_EVENT &&
        event_id == IP_EVENT_STA_GOT_IP
    )
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        retry_count = 0;

        wifi_connected = true;

        ESP_LOGI(
            TAG,
            "======================================"
        );

        ESP_LOGI(
            TAG,
            "WiFi conectado!"
        );

        ESP_LOGI(
            TAG,
            "IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        ESP_LOGI(
            TAG,
            "======================================"
        );

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        return;
    }
}

void wifi_manager_init(void)
{
    ESP_LOGI(
        TAG,
        "Inicializando WiFi..."
    );

    /*
     * Inicializa NVS.
     */
    esp_err_t ret = nvs_flash_init();

    if (
        ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    /*
     * Cria grupo de eventos.
     */
    wifi_event_group =
        xEventGroupCreate();

    /*
     * Inicializa TCP/IP.
     */
    ESP_ERROR_CHECK(
        esp_netif_init()
    );

    /*
     * Cria o event loop padrão.
     */
    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    /*
     * Cria interface Wi-Fi STA.
     */
    esp_netif_create_default_wifi_sta();

    /*
     * Configuração padrão do driver Wi-Fi.
     */
    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    /*
     * Registra eventos Wi-Fi.
     */
    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    /*
     * Registra eventos de IP.
     */
    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    /*
     * Configuração da estação.
     */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,

            .threshold.authmode =
                WIFI_AUTH_WPA2_PSK,

            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        }
    };

    /*
     * Define modo Station.
     */
    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    /*
     * Define credenciais.
     */
    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    /*
     * Inicia Wi-Fi.
     */
    ESP_ERROR_CHECK(
        esp_wifi_start()
    );
}

bool wifi_manager_is_connected(void)
{
    return wifi_connected;
}