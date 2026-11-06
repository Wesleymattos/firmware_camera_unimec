#include "modem.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "esp_modem_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* UART do SIM7080G */
#define MODEM_UART_PORT UART_NUM_1
#define MODEM_TX_GPIO   1
#define MODEM_RX_GPIO   41
#define MODEM_BAUD_RATE 115200

/* APN da operadora */
#define MODEM_APN "iot.datatem.com.br"

/* Tempos de recuperação */
#define MODEM_BOOT_DELAY_MS       3000
#define MODEM_PPP_TIMEOUT_MS      60000
#define MODEM_COMMAND_DELAY_MS    2000
#define MODEM_RETRY_INITIAL_MS    5000
#define MODEM_RETRY_MAX_MS        30000

static const char *TAG = "MODEM";

static esp_modem_dce_t *modem_dce = NULL;
static esp_netif_t *modem_netif = NULL;

static volatile bool modem_connected = false;

static bool modem_event_registered = false;
static bool modem_resources_created = false;

static TaskHandle_t modem_task_handle = NULL;

/*
 * Handler dos eventos de IP do PPP.
 */
static void modem_ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;

    ESP_LOGI(TAG, "Evento IP recebido: base=%s, id=%ld",
         event_base, (long)event_id);

    if (event_base != IP_EVENT) {
        return;
    }

    if (event_id == IP_EVENT_PPP_GOT_IP) {

        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        modem_connected = true;

        ESP_LOGI(TAG, "================================");
        ESP_LOGI(TAG, "SIM7080G CONECTADO!");
        ESP_LOGI(TAG, "IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR,
                 IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "================================");

    } else if (event_id == IP_EVENT_PPP_LOST_IP) {

        modem_connected = false;

        ESP_LOGW(TAG, "PPP perdeu o endereco IP");
    }
}

/*
 * Cria os recursos do modem.
 *
 * Esta função é executada uma vez e também pode
 * ser chamada novamente caso a criação falhe.
 */
static esp_err_t modem_create_resources(void)
{
    esp_err_t err;

    if (!modem_event_registered) {

        err = esp_event_handler_register(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            modem_ip_event_handler,
            NULL
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG,
                     "Falha ao registrar eventos IP: %s",
                     esp_err_to_name(err));
            return err;
        }

        modem_event_registered = true;
    }

    if (modem_netif == NULL) {

        esp_netif_config_t netif_config =
            ESP_NETIF_DEFAULT_PPP();

        modem_netif = esp_netif_new(&netif_config);

        if (modem_netif == NULL) {
            ESP_LOGE(TAG, "Falha ao criar interface PPP");
            return ESP_FAIL;
        }
    }

    if (modem_dce == NULL) {

        esp_modem_dce_config_t dce_config =
            ESP_MODEM_DCE_DEFAULT_CONFIG(MODEM_APN);

        esp_modem_dte_config_t dte_config =
            ESP_MODEM_DTE_DEFAULT_CONFIG();

        dte_config.uart_config.port_num =
            MODEM_UART_PORT;

        dte_config.uart_config.tx_io_num =
            MODEM_TX_GPIO;

        dte_config.uart_config.rx_io_num =
            MODEM_RX_GPIO;

        dte_config.uart_config.baud_rate =
            MODEM_BAUD_RATE;

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

            ESP_LOGE(TAG, "Falha ao criar esp_modem");

            esp_netif_destroy(modem_netif);
            modem_netif = NULL;

            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "SIM7080G criado");

        ESP_LOGI(
            TAG,
            "UART%d TX GPIO%d RX GPIO%d - %d baud",
            (int)MODEM_UART_PORT,
            MODEM_TX_GPIO,
            MODEM_RX_GPIO,
            MODEM_BAUD_RATE
        );

        ESP_LOGI(TAG, "APN: %s", MODEM_APN);
    }

    modem_resources_created = true;

    return ESP_OK;
}

/*
 * Tarefa independente do SIM7080G.
 *
 * Mantém o PPP conectado e tenta recuperar
 * a conexão caso ela seja perdida.
 */
static void modem_task(void *arg)
{
    (void)arg;

    uint32_t retry_delay_ms = MODEM_RETRY_INITIAL_MS;
    bool first_attempt = true;

    while (1) {

        /*
         * Garante que os recursos do modem existam.
         */
        if (!modem_resources_created) {

            ESP_LOGI(TAG, "Criando recursos do modem...");

            esp_err_t err = modem_create_resources();

            if (err != ESP_OK) {

                ESP_LOGW(
                    TAG,
                    "Falha na criacao. Nova tentativa em %lu ms",
                    (unsigned long)retry_delay_ms
                );

                vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));

                if (retry_delay_ms < MODEM_RETRY_MAX_MS) {
                    retry_delay_ms *= 2;

                    if (retry_delay_ms > MODEM_RETRY_MAX_MS) {
                        retry_delay_ms = MODEM_RETRY_MAX_MS;
                    }
                }

                continue;
            }

            /*
             * Dá um tempo para o modem inicializar.
             */
            ESP_LOGI(TAG, "Aguardando modem estabilizar...");

            vTaskDelay(pdMS_TO_TICKS(MODEM_BOOT_DELAY_MS));
            ESP_LOGI(TAG, "Estabilizacao concluida. Iniciando PPP...");
        }

        /*
         * Se já está conectado, apenas monitora.
         */
        if (modem_connected) {

            retry_delay_ms = MODEM_RETRY_INITIAL_MS;

            vTaskDelay(pdMS_TO_TICKS(1000));

            continue;
        }

        ESP_LOGW(TAG, "Iniciando tentativa de conexao PPP");

        /*
         * Nas tentativas posteriores, tenta voltar
         * ao modo comando antes de ativar o PPP.
         */
        if (!first_attempt) {

            ESP_LOGI(TAG, "Solicitando modo comando...");

            esp_err_t cmd_err = esp_modem_set_mode(
                modem_dce,
                ESP_MODEM_MODE_COMMAND
            );

            if (cmd_err != ESP_OK) {

                ESP_LOGW(
                    TAG,
                    "Nao foi possivel ativar modo comando: %s",
                    esp_err_to_name(cmd_err)
                );
            }

            vTaskDelay(
                pdMS_TO_TICKS(MODEM_COMMAND_DELAY_MS)
            );
        }

        first_attempt = false;

        /*
         * Inicia o modo de dados PPP.
         */
ESP_LOGI(TAG, "Chamando esp_modem_set_mode(DATA)...");

esp_err_t err = esp_modem_set_mode(
    modem_dce,
    ESP_MODEM_MODE_DATA
);

ESP_LOGI(TAG, "Resultado esp_modem_set_mode(DATA): %s",
         esp_err_to_name(err));

        if (err != ESP_OK) {

            ESP_LOGE(
                TAG,
                "Falha ao iniciar PPP: %s",
                esp_err_to_name(err)
            );

        } else {

            ESP_LOGI(
                TAG,
                "PPP iniciado. Aguardando endereco IP..."
            );

            /*
             * Espera pelo IP sem bloquear a aplicação
             * principal. A tarefa do modem aguarda aqui.
             */
            uint32_t elapsed_ms = 0;

while (!modem_connected &&
       elapsed_ms < MODEM_PPP_TIMEOUT_MS) {

    vTaskDelay(pdMS_TO_TICKS(1000));

    elapsed_ms += 1000;

    if ((elapsed_ms % 5000) == 0) {
        ESP_LOGI(TAG, "Aguardando IP PPP... %lu ms",
                 (unsigned long)elapsed_ms);
    }
}

            if (modem_connected) {

                ESP_LOGI(TAG, "Conexao PPP estabelecida!");

                retry_delay_ms = MODEM_RETRY_INITIAL_MS;

                continue;
            }

            ESP_LOGW(
                TAG,
                "Timeout aguardando IP PPP (%lu ms)",
                (unsigned long)MODEM_PPP_TIMEOUT_MS
            );
        }

        /*
         * A tentativa falhou.
         * Espera antes de tentar novamente.
         */
        ESP_LOGW(
            TAG,
            "Nova tentativa PPP em %lu ms",
            (unsigned long)retry_delay_ms
        );

        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));

        if (retry_delay_ms < MODEM_RETRY_MAX_MS) {

            retry_delay_ms *= 2;

            if (retry_delay_ms > MODEM_RETRY_MAX_MS) {
                retry_delay_ms = MODEM_RETRY_MAX_MS;
            }
        }
    }
}

/*
 * Inicializa o serviço do modem.
 *
 * esp_netif_init() e o event loop padrão devem
 * estar inicializados antes desta função.
 */
void modem_init(void)
{
    if (modem_task_handle != NULL) {

        ESP_LOGW(TAG, "Tarefa do modem ja iniciada");

        return;
    }

    ESP_LOGI(TAG, "Iniciando tarefa do SIM7080G");

    BaseType_t result = xTaskCreate(
        modem_task,
        "modem_task",
        6144,
        NULL,
        5,
        &modem_task_handle
    );

    if (result != pdPASS) {

        modem_task_handle = NULL;

        ESP_LOGE(TAG, "Falha ao criar tarefa do modem");

        return;
    }

    ESP_LOGI(TAG, "Tarefa do modem iniciada");
}

/*
 * Retorna true quando o PPP recebeu IP.
 */
bool modem_is_connected(void)
{
    return modem_connected;
}

/*
 * A reconexão agora é feita pela tarefa dedicada.
 * Mantemos esta função para compatibilidade com
 * o app_main() atual.
 */
void modem_process(void)
{
    /* Não é necessário processar o modem aqui. */
}