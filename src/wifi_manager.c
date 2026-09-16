#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/apps/sntp.h"

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#include "config.h"

static const char *TAG = "WIFI";

/*
 * Bits utilizados para controlar o estado da conexão.
 */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/*
 * NVS utilizado para guardar a última
 * referência válida de data/hora.
 */
#define TIME_NVS_NAMESPACE "system_time"
#define TIME_NVS_KEY       "last_timestamp"

static EventGroupHandle_t wifi_event_group = NULL;

static int retry_count = 0;

/*
 * Indica se o ESP32 recebeu IP.
 */
static bool wifi_connected = false;


/*
 * =========================================================
 * RECUPERA ÚLTIMA DATA/HORA DO NVS
 * =========================================================
 */

static bool wifi_load_saved_time(void)
{
    nvs_handle_t handle;

    esp_err_t ret =
        nvs_open(
            TIME_NVS_NAMESPACE,
            NVS_READONLY,
            &handle
        );

    if (ret != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Nao foi possivel abrir NVS para recuperar hora: %s",
            esp_err_to_name(ret)
        );

        return false;
    }

    int64_t saved_timestamp = 0;

    ret =
        nvs_get_i64(
            handle,
            TIME_NVS_KEY,
            &saved_timestamp
        );

    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(
            TAG,
            "Nenhuma referencia de data/hora salva no NVS."
        );

        return false;
    }

    if (ret != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Erro ao ler data/hora do NVS: %s",
            esp_err_to_name(ret)
        );

        return false;
    }

    /*
     * Verifica se o timestamp parece válido.
     */
    if (saved_timestamp < 1577836800)
    {
        ESP_LOGW(
            TAG,
            "Timestamp salvo no NVS parece invalido."
        );

        return false;
    }

    /*
     * Restaura o relógio do sistema.
     */
    struct timeval tv;

    tv.tv_sec = (time_t)saved_timestamp;
    tv.tv_usec = 0;

    settimeofday(
        &tv,
        NULL
    );

    /*
     * Define o fuso horário.
     */
    setenv(
        "TZ",
        "America/Sao_Paulo",
        1
    );

    tzset();

    /*
     * Mostra a data/hora recuperada.
     */
    time_t now = 0;

    struct tm timeinfo = {0};

    time(&now);

    localtime_r(
        &now,
        &timeinfo
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );

    ESP_LOGI(
        TAG,
        "Data/hora recuperada do NVS!"
    );

    ESP_LOGI(
        TAG,
        "Data: %02d/%02d/%04d",
        timeinfo.tm_mday,
        timeinfo.tm_mon + 1,
        timeinfo.tm_year + 1900
    );

    ESP_LOGI(
        TAG,
        "Hora: %02d:%02d:%02d",
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );

    return true;
}


/*
 * =========================================================
 * SALVA DATA/HORA ATUAL NO NVS
 * =========================================================
 */

static bool wifi_save_current_time(void)
{
    time_t now = 0;

    time(&now);

    /*
     * Verifica se o relógio possui uma data válida.
     */
    if (now < 1577836800)
    {
        ESP_LOGW(
            TAG,
            "Relogio invalido. Data/hora nao sera salva."
        );

        return false;
    }

    nvs_handle_t handle;

    esp_err_t ret =
        nvs_open(
            TIME_NVS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel abrir NVS para salvar hora: %s",
            esp_err_to_name(ret)
        );

        return false;
    }

    ret =
        nvs_set_i64(
            handle,
            TIME_NVS_KEY,
            (int64_t)now
        );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro ao salvar timestamp no NVS: %s",
            esp_err_to_name(ret)
        );

        nvs_close(handle);

        return false;
    }

    ret =
        nvs_commit(handle);

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro ao confirmar timestamp no NVS: %s",
            esp_err_to_name(ret)
        );

        nvs_close(handle);

        return false;
    }

    nvs_close(handle);

    ESP_LOGI(
        TAG,
        "Data/hora atual salva no NVS. Timestamp: %lld",
        (long long)now
    );

    return true;
}


/*
 * =========================================================
 * SINCRONIZAÇÃO DE DATA E HORA
 * =========================================================
 */

static void wifi_sync_time(void)
{
    ESP_LOGI(
        TAG,
        "Iniciando sincronizacao de data e hora..."
    );

    /*
     * Define o fuso horario de Sao Paulo.
     */
    setenv(
        "TZ",
        "America/Sao_Paulo",
        1
    );

    tzset();

    /*
     * Configura o servidor NTP.
     */
    sntp_setoperatingmode(
        SNTP_OPMODE_POLL
    );

    sntp_setservername(
        0,
        "pool.ntp.org"
    );

    /*
     * Inicia o SNTP.
     */
    sntp_init();

    /*
     * Aguarda ate 10 segundos pela sincronizacao.
     */
    time_t now = 0;

    struct tm timeinfo = {0};

    for (int i = 0; i < 100; i++)
    {
        time(&now);

        localtime_r(
            &now,
            &timeinfo
        );

        /*
         * Ano >= 2020 significa que
         * o relogio ja foi sincronizado.
         */
        if (timeinfo.tm_year >= (2020 - 1900))
        {
            ESP_LOGI(
                TAG,
                "======================================"
            );

            ESP_LOGI(
                TAG,
                "Data e hora sincronizadas!"
            );

            ESP_LOGI(
                TAG,
                "Data: %02d/%02d/%04d",
                timeinfo.tm_mday,
                timeinfo.tm_mon + 1,
                timeinfo.tm_year + 1900
            );

            ESP_LOGI(
                TAG,
                "Hora: %02d:%02d:%02d",
                timeinfo.tm_hour,
                timeinfo.tm_min,
                timeinfo.tm_sec
            );

            ESP_LOGI(
                TAG,
                "======================================"
            );

            /*
             * Salva a referência sincronizada.
             */
            if (wifi_save_current_time())
            {
                ESP_LOGI(
                    TAG,
                    "Referencia de data/hora persistida com sucesso!"
                );
            }
            else
            {
                ESP_LOGE(
                    TAG,
                    "FALHA ao persistir data/hora no NVS!"
                );
            }

            return;
        }

        vTaskDelay(
            pdMS_TO_TICKS(100)
        );
    }

    ESP_LOGW(
        TAG,
        "Nao foi possivel sincronizar a data e hora pelo NTP."
    );
}


/*
 * =========================================================
 * EVENTOS DO WIFI
 * =========================================================
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

        /*
         * Agora que temos rede,
         * sincroniza data e hora.
         */
        wifi_sync_time();

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT
        );

        return;
    }
}


/*
 * =========================================================
 * INICIALIZAÇÃO DO WIFI
 * =========================================================
 */

void wifi_manager_init(void)
{
    ESP_LOGI(
        TAG,
        "Inicializando WiFi..."
    );

    /*
     * Inicializa NVS.
     */
    esp_err_t ret =
        nvs_flash_init();

    if (
        ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND
    )
    {
        ESP_ERROR_CHECK(
            nvs_flash_erase()
        );

        ret =
            nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    /*
     * Recupera a última referência
     * de data/hora salva.
     */
    wifi_load_saved_time();

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


/*
 * =========================================================
 * STATUS DO WIFI
 * =========================================================
 */

bool wifi_manager_is_connected(void)
{
    return wifi_connected;
}