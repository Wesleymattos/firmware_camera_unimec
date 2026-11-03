
#include "modem.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "esp_modem_config.h"

static const char *TAG = "MODEM";

/* UART escolhida para o SIM7080G */
#define MODEM_UART_PORT UART_NUM_1
#define MODEM_TX_GPIO   1
#define MODEM_RX_GPIO   41
#define MODEM_BAUD_RATE 115200

/*
 * Marcador temporário.
 * Não entrar no modo PPP até configurar o APN correto.
 */
#define MODEM_APN "CHANGE_ME"

static esp_modem_dce_t *modem_dce = NULL;
static esp_netif_t *modem_netif = NULL;

static bool modem_connected = false;
static bool modem_initialized = false;

static void modem_ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    if (event_base != IP_EVENT) {
        return;
    }

    if (event_id == IP_EVENT_PPP_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        modem_connected = true;

        ESP_LOGI(TAG, "Interface PPP recebeu endereco IP");
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        modem_connected = false;
        ESP_LOGW(TAG, "Interface PPP perdeu o endereco IP");
    }
}

void modem_init(void)
{
    if (modem_initialized) {
        ESP_LOGW(TAG, "Modem ja inicializado");
        return;
    }

    /*
     * Pressupoe que esp_netif_init() e o event loop padrao
     * ja foram inicializados pelo restante do projeto.
     */

    ESP_LOGI(TAG, "Preparando interface do SIM7080G");

    esp_err_t err;

    err = esp_event_handler_register(
        IP_EVENT,
        ESP_EVENT_ANY_ID,
        modem_ip_event_handler,
        NULL
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao registrar eventos IP: %s",
                 esp_err_to_name(err));
        return;
    }

    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_PPP();

    modem_netif = esp_netif_new(&netif_config);

    if (modem_netif == NULL) {
        ESP_LOGE(TAG, "Falha ao criar interface PPP");
        goto fail_event;
    }

    esp_modem_dce_config_t dce_config =
        ESP_MODEM_DCE_DEFAULT_CONFIG(MODEM_APN);

    esp_modem_dte_config_t dte_config =
        ESP_MODEM_DTE_DEFAULT_CONFIG();

    dte_config.uart_config.port_num = MODEM_UART_PORT;
    dte_config.uart_config.tx_io_num = MODEM_TX_GPIO;
    dte_config.uart_config.rx_io_num = MODEM_RX_GPIO;
    dte_config.uart_config.baud_rate = MODEM_BAUD_RATE;

    dte_config.uart_config.flow_control =
        ESP_MODEM_FLOW_CONTROL_NONE;

    dte_config.uart_config.rts_io_num = -1;
    dte_config.uart_config.cts_io_num = -1;

    modem_dce = esp_modem_new(
        &dte_config,
        &dce_config,
        modem_netif
    );

    if (modem_dce == NULL) {
        ESP_LOGE(TAG, "Falha ao criar instancia esp_modem");
        esp_netif_destroy(modem_netif);
        modem_netif = NULL;
        goto fail_event;
    }

    modem_initialized = true;

    ESP_LOGI(TAG, "Interface preparada");
    ESP_LOGI(TAG, "UART%d: TX GPIO%d, RX GPIO%d, %d baud",
             (int)MODEM_UART_PORT,
             MODEM_TX_GPIO,
             MODEM_RX_GPIO,
             MODEM_BAUD_RATE);

    ESP_LOGI(TAG, "PPP ainda nao iniciado; APN pendente");
    return;

fail_event:
    esp_event_handler_unregister(
        IP_EVENT,
        ESP_EVENT_ANY_ID,
        modem_ip_event_handler
    );
}

bool modem_is_connected(void)
{
    return modem_connected;
}

void modem_process(void)
{
    /*
     * Reconexao e manutencao serao implementadas
     * depois de validar a comunicacao com o modem.
     */
}
