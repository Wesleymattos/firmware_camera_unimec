
#include "modem_y7080e.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/uart.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* =========================================================
   CONFIGURACOES
   ========================================================= */

#define MODEM_Y7080E_TX_GPIO       1
#define MODEM_Y7080E_RX_GPIO       41
#define MODEM_Y7080E_RST_GPIO      47

#define MODEM_Y7080E_UART          UART_NUM_1
#define MODEM_Y7080E_BAUDRATE      9600

#define MODEM_Y7080E_APN           "iot.datatem.com.br"

#define MODEM_AT_TIMEOUT           5000

#define MODEM_UART_RX_BUFFER       4096
#define MODEM_UART_TX_BUFFER       4096

#define MODEM_SOCKET_MAX_DATA      700
#define MODEM_SOCKET_HEX_SIZE      (MODEM_SOCKET_MAX_DATA * 2 + 1)

#define MODEM_AT_RESPONSE_SIZE     2048

static const char *TAG = "MODEM_Y7080E";

static bool uart_initialized = false;
static bool modem_connected = false;

static SemaphoreHandle_t modem_mutex = NULL;


/* =========================================================
   FUNCOES INTERNAS
   ========================================================= */

static esp_err_t modem_send_at(
    const char *command,
    char *response,
    size_t response_size,
    uint32_t timeout)
{
    if (!uart_initialized ||
        command == NULL ||
        response == NULL ||
        response_size < 2)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(response, 0, response_size);

    uart_flush_input(MODEM_Y7080E_UART);

    char cmd[1600];

    int cmd_len = snprintf(
        cmd,
        sizeof(cmd),
        "%s\r\n",
        command
    );

    if (cmd_len < 0 || cmd_len >= sizeof(cmd))
        return ESP_ERR_INVALID_SIZE;

    uart_write_bytes(
        MODEM_Y7080E_UART,
        cmd,
        cmd_len
    );

    size_t total = 0;

    TickType_t inicio = xTaskGetTickCount();
    TickType_t limite = pdMS_TO_TICKS(timeout);

    while ((xTaskGetTickCount() - inicio) < limite)
    {
        uint8_t buffer[256];

        int len = uart_read_bytes(
            MODEM_Y7080E_UART,
            buffer,
            sizeof(buffer),
            pdMS_TO_TICKS(100)
        );

        if (len <= 0)
            continue;

        size_t disponivel = response_size - total - 1;

        size_t copiar = (len < disponivel)
            ? len
            : disponivel;

        memcpy(
            &response[total],
            buffer,
            copiar
        );

        total += copiar;
        response[total] = '\0';

        if (strstr(response, "\nOK\r\n") != NULL ||
            strstr(response, "\r\nOK\r\n") != NULL ||
            strstr(response, "\nERROR\r\n") != NULL ||
            strstr(response, "\r\nERROR\r\n") != NULL)
        {
            break;
        }

        if (total >= response_size - 1)
            break;
    }

    ESP_LOGI(TAG, "AT: %s", command);
    ESP_LOGI(TAG, "Resposta: %s", response);

    if (strstr(response, "ERROR") != NULL)
        return ESP_FAIL;

    if (strstr(response, "OK") != NULL)
        return ESP_OK;

    return ESP_ERR_TIMEOUT;
}


/* =========================================================
   CONVERSAO HEXADECIMAL
   ========================================================= */

static int hex_to_byte(char high, char low)
{
    char temp[3] = { high, low, '\0' };

    return (int)strtol(temp, NULL, 16);
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

    vTaskDelay(pdMS_TO_TICKS(3000));

    ESP_LOGI(TAG, "Reset concluido");
}


/* =========================================================
   INICIALIZACAO
   ========================================================= */

esp_err_t modem_y7080e_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Inicializando Y7080E");

    /* GPIO RESET */

    gpio_config_t reset_config = {
        .pin_bit_mask = (1ULL << MODEM_Y7080E_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    err = gpio_config(&reset_config);

    if (err != ESP_OK)
        return err;

    gpio_set_level(MODEM_Y7080E_RST_GPIO, 0);

    /* UART */

    uart_config_t uart_config = {
        .baud_rate = MODEM_Y7080E_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    err = uart_driver_install(
        MODEM_Y7080E_UART,
        MODEM_UART_RX_BUFFER,
        MODEM_UART_TX_BUFFER,
        0,
        NULL,
        0
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro instalando UART: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(
        MODEM_Y7080E_UART,
        &uart_config
    );

    if (err != ESP_OK)
        return err;

    err = uart_set_pin(
        MODEM_Y7080E_UART,
        MODEM_Y7080E_TX_GPIO,
        MODEM_Y7080E_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (err != ESP_OK)
        return err;

    uart_initialized = true;

    modem_mutex = xSemaphoreCreateMutex();

    if (modem_mutex == NULL)
        return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "UART inicializada");
    ESP_LOGI(TAG, "TX GPIO: %d", MODEM_Y7080E_TX_GPIO);
    ESP_LOGI(TAG, "RX GPIO: %d", MODEM_Y7080E_RX_GPIO);

    /* Reset do modem */

    modem_y7080e_reset();

    /* Configuracao do contexto de dados */

    char response[MODEM_AT_RESPONSE_SIZE];

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    err = modem_send_at(
        "AT+CGDCONT=1,\"IP\",\"" MODEM_Y7080E_APN "\"",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err == ESP_OK)
    {
        err = modem_send_at(
            "AT+CGACT=1,1",
            response,
            sizeof(response),
            MODEM_AT_TIMEOUT
        );
    }

    xSemaphoreGive(modem_mutex);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha configurando contexto PDP");
        modem_connected = false;
        return err;
    }

    modem_connected = true;

    ESP_LOGI(TAG, "Camada de transporte inicializada");

    return ESP_OK;
}


/* =========================================================
   ABRIR SOCKET TCP
   ========================================================= */

esp_err_t modem_y7080e_socket_open(int *socket_id)
{
    if (socket_id == NULL || !modem_connected)
        return ESP_ERR_INVALID_ARG;

    char response[MODEM_AT_RESPONSE_SIZE];

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    esp_err_t err = modem_send_at(
        "AT+NSOCR=STREAM,6,0,1",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    xSemaphoreGive(modem_mutex);

    if (err != ESP_OK)
        return err;

    char *p = strstr(response, "+NSOCR:");

    if (p != NULL)
        p = strchr(p, ':') + 1;
    else
        p = response;

    while (*p == ' ' || *p == '\r' || *p == '\n')
        p++;

    char *end = NULL;

    long id = strtol(p, &end, 10);

    if (end == p || id < 0)
    {
        ESP_LOGE(TAG, "Nao foi possivel obter socket ID");
        return ESP_FAIL;
    }

    *socket_id = (int)id;

    ESP_LOGI(TAG, "Socket TCP criado: %d", *socket_id);

    return ESP_OK;
}


/* =========================================================
   CONECTAR SOCKET TCP
   ========================================================= */

esp_err_t modem_y7080e_socket_connect(
    int socket_id,
    const char *server_ip,
    uint16_t server_port)
{
    if (server_ip == NULL)
        return ESP_ERR_INVALID_ARG;

    char command[160];
    char response[MODEM_AT_RESPONSE_SIZE];

    snprintf(
        command,
        sizeof(command),
        "AT+NSOCO=%d,%s,%u",
        socket_id,
        server_ip,
        server_port
    );

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    esp_err_t err = modem_send_at(
        command,
        response,
        sizeof(response),
        30000
    );

    xSemaphoreGive(modem_mutex);

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "Socket %d conectado em %s:%u",
            socket_id,
            server_ip,
            server_port
        );
    }

    return err;
}


/* =========================================================
   ENVIAR DADOS PELO SOCKET
   ========================================================= */

esp_err_t modem_y7080e_socket_send(
    int socket_id,
    const uint8_t *data,
    size_t length)
{
    if (data == NULL || length == 0)
        return ESP_ERR_INVALID_ARG;

    size_t offset = 0;

    char hex_data[MODEM_SOCKET_HEX_SIZE];
    char command[1600];
    char response[512];

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    while (offset < length)
    {
        size_t chunk = length - offset;

        if (chunk > MODEM_SOCKET_MAX_DATA)
            chunk = MODEM_SOCKET_MAX_DATA;

        /* Converte os bytes para hexadecimal */

        for (size_t i = 0; i < chunk; i++)
        {
            snprintf(
                &hex_data[i * 2],
                3,
                "%02X",
                data[offset + i]
            );
        }

        hex_data[chunk * 2] = '\0';

        snprintf(
            command,
            sizeof(command),
            "AT+NSOSD=%d,%u,%s",
            socket_id,
            (unsigned int)chunk,
            hex_data
        );

        esp_err_t err = modem_send_at(
            command,
            response,
            sizeof(response),
            30000
        );

        if (err != ESP_OK)
        {
            xSemaphoreGive(modem_mutex);
            ESP_LOGE(TAG, "Erro enviando dados pelo socket %d", socket_id);
            return err;
        }

        offset += chunk;
    }

    xSemaphoreGive(modem_mutex);

    return ESP_OK;
}


/* =========================================================
   RECEBER DADOS PELO SOCKET
   ========================================================= */

int modem_y7080e_socket_receive(
    int socket_id,
    uint8_t *buffer,
    size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
        return -1;

    size_t requested = buffer_size;

    if (requested > MODEM_SOCKET_MAX_DATA)
        requested = MODEM_SOCKET_MAX_DATA;

    char command[80];
    char response[MODEM_AT_RESPONSE_SIZE];
    char remote_ip[64];
    char hex_data[MODEM_SOCKET_HEX_SIZE];

    int response_socket;
    int data_length;
    int remote_port;
    int remaining;

    snprintf(
        command,
        sizeof(command),
        "AT+NSORF=%d,%u",
        socket_id,
        (unsigned int)requested
    );

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    esp_err_t err = modem_send_at(
        command,
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    xSemaphoreGive(modem_mutex);

    if (err != ESP_OK)
        return -1;

    char *p = strstr(response, "+NSORF:");

    if (p == NULL)
        return 0;

    int parsed = sscanf(
        p,
        "+NSORF: %d,%d,%63[^,],%d,%1400[^,],%d",
        &response_socket,
        &data_length,
        remote_ip,
        &remote_port,
        hex_data,
        &remaining
    );

    if (parsed != 6 || response_socket != socket_id)
    {
        ESP_LOGE(TAG, "Resposta NSORF invalida");
        return -1;
    }

    if (data_length <= 0)
        return 0;

    if ((size_t)data_length > buffer_size)
        data_length = buffer_size;

    if (strlen(hex_data) < (size_t)data_length * 2)
    {
        ESP_LOGE(TAG, "Dados hexadecimais incompletos");
        return -1;
    }

    for (int i = 0; i < data_length; i++)
    {
        buffer[i] = (uint8_t)hex_to_byte(
            hex_data[i * 2],
            hex_data[i * 2 + 1]
        );
    }

    ESP_LOGI(
        TAG,
        "Socket %d recebeu %d bytes",
        socket_id,
        data_length
    );

    return data_length;
}


/* =========================================================
   FECHAR SOCKET
   ========================================================= */

esp_err_t modem_y7080e_socket_close(int socket_id)
{
    char command[64];
    char response[512];

    snprintf(
        command,
        sizeof(command),
        "AT+NSOCL=%d",
        socket_id
    );

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    esp_err_t err = modem_send_at(
        command,
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    xSemaphoreGive(modem_mutex);

    if (err == ESP_OK)
        ESP_LOGI(TAG, "Socket %d fechado", socket_id);

    return err;
}


/* =========================================================
   STATUS
   ========================================================= */

bool modem_y7080e_is_connected(void)
{
    return modem_connected;
}
