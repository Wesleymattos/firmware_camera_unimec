
#include "modem_y7080e.h"

#include <stdbool.h>

#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_modem_api.h"
#include "esp_modem_config.h"


/* =========================================================
   CONFIGURACOES
   ========================================================= */

#define MODEM_Y7080E_TX_GPIO       1
#define MODEM_Y7080E_RX_GPIO       41
#define MODEM_Y7080E_RST_GPIO      47

#define MODEM_Y7080E_UART          UART_NUM_1
#define MODEM_Y7080E_BAUDRATE      9600

#define MODEM_Y7080E_APN           "iot.datatem.com.br"

#define MODEM_AT_TIMEOUT                 5000



static const char *TAG = "MODEM_Y7080E";

static esp_modem_dce_t *modem_dce = NULL;
static esp_netif_t *modem_netif = NULL;

static volatile bool modem_connected = false;
static bool event_handlers_registered = false;


/* =========================================================
   ENVIO DE COMANDOS AT
   ========================================================= */

static esp_err_t modem_y7080e_send_at(
    const char *command,
    char *response,
    size_t response_size,
    uint32_t timeout)
{
    if (modem_dce == NULL || response == NULL || response_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(response, 0, response_size);

    esp_err_t err = esp_modem_at(
        modem_dce,
        command,
        response,
        timeout
    );

    ESP_LOGI(TAG, "Comando: %s", command);

    ESP_LOGI(
        TAG,
        "Resposta (%s): %s",
        esp_err_to_name(err),
        response
    );

    return err;
}


/* =========================================================
   EVENTOS PPP
   ========================================================= */

static void modem_y7080e_ip_event(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    if (event_id == IP_EVENT_PPP_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        modem_connected = true;

        ESP_LOGI(TAG, "PPP conectado!");

        ESP_LOGI(
            TAG,
            "IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );
    }
    else if (event_id == IP_EVENT_PPP_LOST_IP)
    {
        modem_connected = false;

        ESP_LOGW(TAG, "Conexao PPP perdida");
    }
}


/* =========================================================
   RESET FISICO
   ========================================================= */

void modem_y7080e_reset(void)
{
    ESP_LOGW(TAG, "Executando reset fisico do Y7080E");

    gpio_set_level(MODEM_Y7080E_RST_GPIO, 1);

    vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level(MODEM_Y7080E_RST_GPIO, 0);

    ESP_LOGI(TAG, "Pulso de RESET concluido");

    vTaskDelay(pdMS_TO_TICKS(3000));
}





/* =========================================================
   INICIALIZACAO
   ========================================================= */

esp_err_t modem_y7080e_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Inicializando modem Y7080E");

    ESP_LOGI(TAG, "APN: %s", MODEM_Y7080E_APN);


    /* -----------------------------------------------------
       1. Configura GPIO RESET
       ----------------------------------------------------- */

    gpio_config_t reset_config = {
        .pin_bit_mask = (1ULL << MODEM_Y7080E_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    err = gpio_config(&reset_config);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro configurando GPIO RESET");
        return err;
    }

    gpio_set_level(MODEM_Y7080E_RST_GPIO, 0);

    vTaskDelay(pdMS_TO_TICKS(200));


    /* -----------------------------------------------------
       2. Reset fisico
       ----------------------------------------------------- */
modem_y7080e_reset();

ESP_LOGI(TAG, "Reset do Y7080E concluido");


    /* -----------------------------------------------------
       3. Registra eventos PPP
       ----------------------------------------------------- */

    if (!event_handlers_registered)
    {
        err = esp_event_handler_register(
            IP_EVENT,
            ESP_EVENT_ANY_ID,
            modem_y7080e_ip_event,
            NULL
        );

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Erro registrando eventos IP");
            return err;
        }

        event_handlers_registered = true;
    }


    /* -----------------------------------------------------
       4. Cria interface PPP
       ----------------------------------------------------- */

    esp_netif_config_t netif_config =
        ESP_NETIF_DEFAULT_PPP();

    modem_netif = esp_netif_new(&netif_config);

    if (modem_netif == NULL)
    {
        ESP_LOGE(TAG, "Falha criando interface PPP");
        return ESP_FAIL;
    }


    /* -----------------------------------------------------
       5. Configuracao DCE
       ----------------------------------------------------- */

    esp_modem_dce_config_t dce_config =
        ESP_MODEM_DCE_DEFAULT_CONFIG(MODEM_Y7080E_APN);

    dce_config.protocol_type = "IP";
    dce_config.context_id = 1;


    /* -----------------------------------------------------
       6. Configuracao UART
       ----------------------------------------------------- */

    esp_modem_dte_config_t dte_config =
        ESP_MODEM_DTE_DEFAULT_CONFIG();

    dte_config.uart_config.port_num = MODEM_Y7080E_UART;

    dte_config.uart_config.tx_io_num =
        MODEM_Y7080E_TX_GPIO;

    dte_config.uart_config.rx_io_num =
        MODEM_Y7080E_RX_GPIO;

    dte_config.uart_config.baud_rate =
        MODEM_Y7080E_BAUDRATE;

    dte_config.uart_config.flow_control =
        ESP_MODEM_FLOW_CONTROL_NONE;

    dte_config.uart_config.rts_io_num = -1;
    dte_config.uart_config.cts_io_num = -1;

    ESP_LOGI(TAG, "UART TX: GPIO%d", MODEM_Y7080E_TX_GPIO);
    ESP_LOGI(TAG, "UART RX: GPIO%d", MODEM_Y7080E_RX_GPIO);
    ESP_LOGI(TAG, "UART baudrate: %d", MODEM_Y7080E_BAUDRATE);


    /* -----------------------------------------------------
       7. Cria DCE
       ----------------------------------------------------- */

    modem_dce = esp_modem_new(
        &dte_config,
        &dce_config,
        modem_netif
    );

    if (modem_dce == NULL)
    {
        ESP_LOGE(TAG, "Falha criando DCE");

        esp_netif_destroy(modem_netif);
        modem_netif = NULL;

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DCE criado com sucesso");


    /* -----------------------------------------------------
       8. Testa comunicacao AT
       ----------------------------------------------------- */

    char response[256];

    ESP_LOGI(TAG, "Testando comunicacao AT...");

    err = modem_y7080e_send_at(
        "AT",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha na comunicacao AT");
        return err;
    }


 



ESP_LOGI(TAG, "Consultando estado da rede...");

modem_y7080e_send_at(
    "AT+CSQ",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_y7080e_send_at(
    "AT+CEREG?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_y7080e_send_at(
    "AT+CGATT?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_y7080e_send_at(
    "AT+CGDCONT?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

ESP_LOGI(TAG, "Aguardando registro na rede NB-IoT...");

bool registrado = false;

for (int tentativa = 1; tentativa <= 24; tentativa++)
{
    err = modem_y7080e_send_at(
        "AT+CEREG?",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err == ESP_OK &&
        (strstr(response, ",1") != NULL ||
         strstr(response, ",5") != NULL))
    {
        registrado = true;
        ESP_LOGI(TAG, "Modem registrado na rede!");
        break;
    }

    ESP_LOGW(
        TAG,
        "Aguardando registro... tentativa %d/24",
        tentativa
    );

    vTaskDelay(pdMS_TO_TICKS(5000));
}

if (!registrado)
{
    ESP_LOGE(TAG, "Timeout aguardando registro na rede!");
    return ESP_FAIL;
}

ESP_LOGI(TAG, "Verificando attach...");

bool attached = false;

for (int tentativa = 1; tentativa <= 10; tentativa++)
{
    err = modem_y7080e_send_at(
        "AT+CGATT?",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err == ESP_OK && strstr(response, "+CGATT:1") != NULL)
    {
        attached = true;
        ESP_LOGI(TAG, "Attach confirmado!");
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
}

if (!attached)
{
    ESP_LOGE(TAG, "Falha no attach da rede!");
    return ESP_FAIL;
}

ESP_LOGI(TAG, "Registro e attach confirmados!");


ESP_LOGI(TAG, "Ativando contexto PDP...");

err = modem_y7080e_send_at(
    "AT+CGACT=1,1",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha ativando contexto PDP!");
    return err;
}

vTaskDelay(pdMS_TO_TICKS(1000));

modem_y7080e_send_at(
    "AT+CGACT?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_y7080e_send_at(
    "AT+CGPADDR=1",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_y7080e_send_at(
    "AT+CGCONTRDP=1",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

ESP_LOGI(TAG, "Iniciando conexao PPP...");

err = esp_modem_set_mode(
    modem_dce,
    ESP_MODEM_MODE_DATA
);

if (err != ESP_OK)
{
    ESP_LOGE(
        TAG,
        "Falha ao iniciar PPP: %s",
        esp_err_to_name(err)
    );

    return err;
}

ESP_LOGI(TAG, "PPP iniciado. Aguardando endereco IP...");



return ESP_OK;

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ativando PPP: %s",
            esp_err_to_name(err)
        );

        modem_connected = false;

        return err;
    }

    ESP_LOGI(TAG, "Modo PPP solicitado");

    return ESP_OK;
}


/* =========================================================
   STATUS DA CONEXAO
   ========================================================= */

bool modem_y7080e_is_connected(void)
{
    return modem_connected;
}
