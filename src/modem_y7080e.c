
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
#include "event_manager.h"

/* =========================================================
   CONFIGURACOES
   ========================================================= */

#define MODEM_Y7080E_TX_GPIO       1
#define MODEM_Y7080E_RX_GPIO       41
#define MODEM_Y7080E_RST_GPIO      47

#define MODEM_Y7080E_UART          UART_NUM_1
#define MODEM_Y7080E_BAUDRATE 9600
#define MODEM_Y7080E_FAST_BAUDRATE 115200

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
static volatile bool mqtt_connected = false;

static SemaphoreHandle_t modem_mutex = NULL;

static size_t socket_pending[2] = {0, 0};

static int hex_to_byte(char high, char low);


static void modem_parse_urc(const char *data)
{
    const char *p = data;

    /* =========================================
       SOCKET TCP
       ========================================= */

    while ((p = strstr(p, "+NSONMI:")) != NULL)
    {
        int socket_id;
        unsigned int length;

        if (sscanf(p, "+NSONMI:%d,%u",
                   &socket_id, &length) == 2)
        {
            if (socket_id >= 0 && socket_id < 2)
            {
                socket_pending[socket_id] = length;

                ESP_LOGI(TAG,
                         "Socket %d: %u bytes pendentes",
                         socket_id,
                         length);
            }
        }

        p += strlen("+NSONMI:");
    }

    /* =========================================
       MQTT - CONFIRMACAO DE ASSINATURA
       ========================================= */

    p = data;

    while ((p = strstr(p, "+MQSUBACK:")) != NULL)
    {
        ESP_LOGI(TAG, "MQTT SUBACK recebido: %s", p);

        p += strlen("+MQSUBACK:");
    }

       /* =========================================
       MQTT - MENSAGEM RECEBIDA
       ========================================= */

    p = data;

    while ((p = strstr(p, "+MQPUB:")) != NULL)
    {
        int client_id;
        int qos, retained, dup, message_len;
        int consumed = 0;

        char topic[128] = {0};
        char message[32] = {0};

        int parsed = sscanf(
            p,
            "+MQPUB:%d,%127[^,],%d,%d,%d,%d,%n",
            &client_id,
            topic,
            &qos,
            &retained,
            &dup,
            &message_len,
            &consumed
        );

        if (parsed == 6 &&
            consumed > 0 &&
            message_len > 0 &&
            message_len < sizeof(message))
        {
            /* Remove aspas do tópico, se existirem */
            size_t topic_len = strlen(topic);

            if (topic_len >= 2 &&
                topic[0] == '"' &&
                topic[topic_len - 1] == '"')
            {
                memmove(topic, topic + 1, topic_len - 2);
                topic[topic_len - 2] = '\0';
            }

const char *hex = p + consumed;

bool hex_valido = true;

size_t hex_available = strlen(hex);

if (hex_available < (size_t)(message_len * 2))
{
    hex_valido = false;
}

            for (int i = 0; i < message_len * 2; i++)
            {
                char c = hex[i];

                if (!((c >= '0' && c <= '9') ||
                      (c >= 'A' && c <= 'F') ||
                      (c >= 'a' && c <= 'f')))
                {
                    hex_valido = false;
                    break;
                }
            }

            if (hex_valido)
            {
                for (int i = 0; i < message_len; i++)
                {
                    message[i] = (char)hex_to_byte(
                        hex[i * 2],
                        hex[i * 2 + 1]
                    );
                }

                message[message_len] = '\0';

                ESP_LOGI(
                    TAG,
                    "MQTT recebido: topico=%s, mensagem=%s",
                    topic,
                    message
                );

                if (strcmp(topic, "unimec/camera-01/command") == 0)
                {
                    if (strcmp(message, "ON") == 0)
                    {
                        ESP_LOGW(TAG, "Comando MQTT ON recebido!");
                        event_manager_acionar();
                    }
                    else if (strcmp(message, "OFF") == 0)
                    {
                        ESP_LOGW(TAG, "Comando MQTT OFF recebido!");
                        event_manager_desarmar();
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Comando MQTT desconhecido");
                    }
                }
            }
            else
            {
                ESP_LOGW(TAG, "Payload MQTT hexadecimal invalido");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Cabecalho MQTT invalido");
        }

        p += strlen("+MQPUB:");
    }
    /* =========================================
       MQTT - DESCONECTADO
       ========================================= */

    if (strstr(data, "+MQDISON:") != NULL)
    {
        ESP_LOGW(TAG, "Modem informou desconexao MQTT!");
        mqtt_connected = false;
    }
}




size_t modem_y7080e_socket_available(int socket_id)
{
    if (socket_id < 0 || socket_id >= 2)
        return 0;

    size_t pending = 0;

    if (xSemaphoreTake(modem_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        pending = socket_pending[socket_id];
        xSemaphoreGive(modem_mutex);
    }

    return pending;
}


void modem_y7080e_process_mqtt(void)
{
    static char pending[MODEM_AT_RESPONSE_SIZE];
    static size_t pending_len = 0;

    if (!uart_initialized || modem_mutex == NULL)
        return;

    if (xSemaphoreTake(modem_mutex, 0) != pdTRUE)
        return;

    uint8_t buffer[256];

    int len = uart_read_bytes(
        MODEM_Y7080E_UART,
        buffer,
        sizeof(buffer),
        0
    );

    if (len > 0)
    {
        if (pending_len + len >= sizeof(pending))
        {
            ESP_LOGW(TAG, "Buffer MQTT cheio. Limpando dados pendentes.");
            pending_len = 0;
        }

        memcpy(&pending[pending_len], buffer, len);
        pending_len += len;

        pending[pending_len] = '\0';

        char *newline;

        while ((newline = memchr(pending, '\n', pending_len)) != NULL)
        {
            size_t line_len = (size_t)(newline - pending) + 1;

            char line[MODEM_AT_RESPONSE_SIZE];

            memcpy(line, pending, line_len);
            line[line_len] = '\0';

            modem_parse_urc(line);

            pending_len -= line_len;

            memmove(
                pending,
                pending + line_len,
                pending_len
            );

            pending[pending_len] = '\0';
        }
    }

    xSemaphoreGive(modem_mutex);
}


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

        /* Detecta confirmação de conexão MQTT */
char *mqtt_ack = strstr(response, "+MQCONNACK:");

if (mqtt_ack != NULL)
{
    int client_id = -1;
    int result = -1;

    if (sscanf(mqtt_ack, "+MQCONNACK: %d,%d", &client_id, &result) == 2)
    {
        if (client_id == 0 && result == 0)
        {
            mqtt_connected = true;
            ESP_LOGI(TAG, "Conexao MQTT confirmada pelo broker!");
        }
        else
        {
            mqtt_connected = false;
            ESP_LOGE(TAG, "MQCONNACK falhou: cliente=%d, resultado=%d",
                     client_id, result);
        }
    }
}

bool comando_mqtt_connect = strstr(command, "AT+MQCON=") != NULL;

bool erro_at =
    strstr(response, "\nERROR\r\n") != NULL ||
    strstr(response, "\r\nERROR\r\n") != NULL;

bool ok_at =
    strstr(response, "\nOK\r\n") != NULL ||
    strstr(response, "\r\nOK\r\n") != NULL;

if (erro_at)
{
    break;
}

if (comando_mqtt_connect)
{
    if (mqtt_connected)
    {
        break;
    }
}
else if (ok_at)
{
    break;
}
        if (total >= response_size - 1)
            break;
    }

    modem_parse_urc(response);

if (strncmp(command, "AT+NSOSD=", 9) == 0)
{
    int socket_id = -1;
    unsigned int bytes = 0;

    sscanf(command, "AT+NSOSD=%d,%u", &socket_id, &bytes);

    ESP_LOGD(TAG,
             "FTP: enviando socket=%d, bytes=%u",
             socket_id,
             bytes);
}
else
{
    ESP_LOGI(TAG, "AT: %s", command);
}

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
 * AGUARDA REGISTRO NA REDE CELULAR
 * Deve ser chamada com modem_mutex adquirido.
 * ========================================================= */

static esp_err_t modem_wait_network(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Aguardando registro na rede celular...");

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout)
    {
        char response[MODEM_AT_RESPONSE_SIZE] = {0};

        esp_err_t err = modem_send_at(
            "AT+CEREG?",
            response,
            sizeof(response),
            MODEM_AT_TIMEOUT
        );

        if (err == ESP_OK)
        {
           int stat = -1;
bool status_encontrado = false;

char *p = response;

while ((p = strstr(p, "+CEREG:")) != NULL)
{
    int n = -1;
    int s = -1;

    if (sscanf(p, "+CEREG: %d,%d", &n, &s) == 2)
    {
        // A consulta AT+CEREG? está retornando n=3.
        // Ignora URCs e valores que não são status válidos.
        if (s >= 0 && s <= 5)
        {
            stat = s;
            status_encontrado = true;
        }
    }

    p += strlen("+CEREG:");
}

if (status_encontrado)
{
    ESP_LOGI(TAG, "Registro celular: %d", stat);

    if (stat == 1 || stat == 5)
    {
        ESP_LOGI(TAG, "Registro na rede confirmado!");
        return ESP_OK;
    }
}
else
{
    ESP_LOGW(TAG, "Resposta CEREG nao reconhecida: %s", response);
}
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGE(TAG, "Timeout aguardando registro celular");

    return ESP_ERR_TIMEOUT;
}


/* =========================================================
 * AGUARDA CONEXAO A REDE DE PACOTES
 * Deve ser chamada com modem_mutex adquirido.
 * ========================================================= */

static esp_err_t modem_wait_packet_attach(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Aguardando conexao de pacotes...");

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout)
    {
        char response[MODEM_AT_RESPONSE_SIZE] = {0};

        esp_err_t err = modem_send_at(
            "AT+CGATT?",
            response,
            sizeof(response),
            MODEM_AT_TIMEOUT
        );

        if (err == ESP_OK)
        {
            int attached = -1;
            char *p = strstr(response, "+CGATT:");

            if (p != NULL &&
                sscanf(p, "+CGATT: %d", &attached) == 1)
            {
                ESP_LOGI(TAG, "Estado CGATT: %d", attached);

                if (attached == 1)
                {
                    ESP_LOGI(TAG, "Conexao de pacotes confirmada!");
                    return ESP_OK;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_LOGE(TAG, "Timeout aguardando CGATT");

    return ESP_ERR_TIMEOUT;
}


/* =========================================================
 * INICIALIZACAO DO Y7080E
 * ========================================================= */

esp_err_t modem_y7080e_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Inicializando Y7080E");

    modem_connected = false;

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
        ESP_LOGE(TAG, "Erro instalando UART: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(MODEM_Y7080E_UART, &uart_config);

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

    /* RESET FISICO */

    modem_y7080e_reset();

    char response[MODEM_AT_RESPONSE_SIZE];

    if (xSemaphoreTake(modem_mutex, portMAX_DELAY) != pdTRUE)
        return ESP_FAIL;

    /* 1. Verifica comunicacao */

    
/* ========================================
 * 1. DETECCAO E CONFIGURACAO DA UART
 * ======================================== */

ESP_LOGI(TAG, "Verificando comunicacao com modem...");

/* Primeiro tenta comunicar em 9600 baud */
err = modem_send_at(
    "AT",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

if (err == ESP_OK)
{
    ESP_LOGI(TAG, "Modem encontrado em 9600 baud");

    /* Consulta velocidade atual */
    err = modem_send_at(
        "AT+IPR?",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;

    /* Solicita mudanca para 115200 */
    ESP_LOGW(TAG, "Alterando modem para 115200 baud...");

    err = modem_send_at(
        "AT+IPR=115200",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;

    /* Altera a UART do ESP32 */
    err = uart_set_baudrate(
        MODEM_Y7080E_UART,
        MODEM_Y7080E_FAST_BAUDRATE
    );

    if (err != ESP_OK)
        goto modem_error;

    uart_flush_input(MODEM_Y7080E_UART);

    vTaskDelay(pdMS_TO_TICKS(300));
}
else
{
    /* Se falhou em 9600, tenta 115200 */
    ESP_LOGW(TAG, "Sem resposta em 9600. Tentando 115200...");

    err = uart_set_baudrate(
        MODEM_Y7080E_UART,
        MODEM_Y7080E_FAST_BAUDRATE
    );

    if (err != ESP_OK)
        goto modem_error;

    uart_flush_input(MODEM_Y7080E_UART);

    vTaskDelay(pdMS_TO_TICKS(300));
}

/* Testa comunicacao na velocidade rapida */
err = modem_send_at(
    "AT",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha na comunicacao UART em 115200 baud!");
    goto modem_error;
}

ESP_LOGI(TAG, "Comunicacao UART em 115200 baud confirmada!");

/* CONSULTA VERSAO DO FIRMWARE */
ESP_LOGI(TAG, "Consultando firmware do Y7080E...");

err = modem_send_at(
    "AT+CGMR",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

if (err == ESP_OK)
{
    ESP_LOGI(TAG, "VERSAO DO FIRMWARE: %s", response);
}
else
{
    ESP_LOGW(TAG, "Nao foi possivel consultar o firmware");
}

/* Confirma velocidade configurada */
err = modem_send_at(
    "AT+IPR?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

if (err != ESP_OK)
    goto modem_error;


/* 2. Verifica SIM */
ESP_LOGI(TAG, "Verificando cartao SIM...");

    err = modem_send_at(
        "AT+CPIN?",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;

    if (strstr(response, "READY") == NULL)
    {
        ESP_LOGE(TAG, "SIM nao esta pronto: %s", response);
        err = ESP_ERR_INVALID_STATE;
        goto modem_error;
    }

    ESP_LOGI(TAG, "SIM pronto");

    /* 3. Configura APN */

    ESP_LOGI(TAG, "Configurando APN...");

    err = modem_send_at(
        "AT+CGDCONT=1,\"IP\",\"" MODEM_Y7080E_APN "\"",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;



ESP_LOGW(TAG, "========== DIAGNOSTICO ==========");

modem_send_at("AT+CSQ", response,
              sizeof(response), MODEM_AT_TIMEOUT);

modem_send_at("AT+CESQ", response,
              sizeof(response), MODEM_AT_TIMEOUT);

modem_send_at("AT+COPS?", response,
              sizeof(response), MODEM_AT_TIMEOUT);

modem_send_at("AT+CEER", response,
              sizeof(response), MODEM_AT_TIMEOUT);

ESP_LOGW(TAG, "=================================");





/* DIAGNOSTICO DE BANDAS NB-IOT */

ESP_LOGI(TAG, "Consultando bandas NB-IOT...");

err = modem_send_at(
    "AT+NBAND?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

ESP_LOGI(TAG, "Bandas NB-IOT: %s", response);


    /* 4. Aguarda registro celular */

    err = modem_wait_network(300000);

    if (err != ESP_OK)
        goto modem_error;

    /* 5. Aguarda conexao de pacotes */

    err = modem_wait_packet_attach(60000);

    if (err != ESP_OK)
        goto modem_error;

   /* 6. Ativa contexto PDP */

ESP_LOGI(TAG, "Ativando contexto PDP...");

err = modem_send_at(
    "AT+CGACT=1,1",
    response,
    sizeof(response),
    60000
);

if (err != ESP_OK)
    goto modem_error;




/* 7. Cria cliente MQTT nativo */

ESP_LOGI(TAG, "Criando cliente MQTT nativo...");

err = modem_send_at(
    "AT+MQNEW=200.234.212.167,1883,5000,1000",
    response,
    sizeof(response),
    30000
);

ESP_LOGI(TAG, "Resultado MQTT NEW: %s", response);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha ao criar cliente MQTT");
    goto modem_error;
}

/* 8. Conecta ao broker MQTT */

ESP_LOGI(TAG, "Conectando ao broker MQTT...");

mqtt_connected = false;

err = modem_send_at(
    "AT+MQCON=0,4,\"unimec-camera-01\",60,1,1,\"unimec/test/will\",0,0,2,\"3838\",\"unimec\",\"UniMec@MQTT2026!\"",
    response,
    sizeof(response),
    30000
);

ESP_LOGI(TAG, "Resultado MQTT CON: %s", response);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha na conexao MQTT");
    goto modem_error;
}


/* 9. Assina o topico de comandos MQTT */

ESP_LOGI(TAG, "Assinando topico de comandos MQTT...");

err = modem_send_at(
    "AT+MQSUB=0,\"unimec/camera-01/command\",0",
    response,
    sizeof(response),
    30000
);



ESP_LOGI(TAG, "Resultado MQTT SUB: %s", response);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha ao assinar topico MQTT");
    goto modem_error;
}


        /* DIAGNOSTICO DE REDE */
ESP_LOGI(TAG, "========== DIAGNOSTICO DE REDE ==========");

modem_send_at(
    "AT+CSQ",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_send_at(
    "AT+CESQ",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_send_at(
    "AT+COPS?",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

modem_send_at(
    "AT+CEER",
    response,
    sizeof(response),
    MODEM_AT_TIMEOUT
);

ESP_LOGI(TAG, "=========================================");

    /* 7. Confirma contexto PDP */

    ESP_LOGI(TAG, "Verificando contexto PDP...");

    err = modem_send_at(
        "AT+CGACT?",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;

    {
        int cid = -1;
        int active = -1;

        char *p = strstr(response, "+CGACT:");

        if (p == NULL ||
            sscanf(p, "+CGACT: %d,%d", &cid, &active) != 2 ||
            cid != 1 || active != 1)
        {
            ESP_LOGE(TAG, "Contexto PDP nao esta ativo: %s",
                     response);

            err = ESP_FAIL;
            goto modem_error;
        }
    }

    /* 8. Ativa notificacoes de socket */

    ESP_LOGI(TAG, "Ativando notificacoes NSONMI...");

    err = modem_send_at(
        "AT+NSONMI=1",
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    if (err != ESP_OK)
        goto modem_error;

    xSemaphoreGive(modem_mutex);

    modem_connected = true;

    ESP_LOGI(TAG, "==================================");
    ESP_LOGI(TAG, "Y7080E INICIALIZADO COM SUCESSO");
    ESP_LOGI(TAG, "Registro celular: OK");
    ESP_LOGI(TAG, "CGATT: OK");
    ESP_LOGI(TAG, "PDP: ATIVO");
    ESP_LOGI(TAG, "==================================");

    return ESP_OK;


modem_error:

    ESP_LOGE(TAG, "Falha na inicializacao: %s",
             esp_err_to_name(err));

    modem_connected = false;

    xSemaphoreGive(modem_mutex);

    return err;
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

 if (end == p || id < 0 || id > 1)
{
    ESP_LOGE(TAG, "Socket ID invalido: %ld", id);
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
    if (socket_id < 0 || socket_id >= 2 ||
        server_ip == NULL ||
        server_port == 0 ||
        !modem_connected)
    {
        return ESP_ERR_INVALID_ARG;
    }

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
    if (data == NULL || length == 0 || !modem_connected)
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
            ESP_LOGE(TAG, "Erro enviando dados pelo socket %d", socket_id);
            xSemaphoreGive(modem_mutex);
            return err;
        }

        /* Confere socket e quantidade aceita */
        char expected[40];

        snprintf(
            expected,
            sizeof(expected),
            "\r\n%d,%u\r\n",
            socket_id,
            (unsigned int)chunk
        );

        if (strstr(response, expected) == NULL)
        {
            ESP_LOGE(
                TAG,
                "Confirmacao invalida. Esperado: %s | Resposta: %s",
                expected,
                response
            );

            xSemaphoreGive(modem_mutex);
            return ESP_FAIL;
        }

        offset += chunk;
    }

    xSemaphoreGive(modem_mutex);

    ESP_LOGI(
        TAG,
        "Envio concluido: socket=%d, bytes=%u",
        socket_id,
        (unsigned int)length
    );

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

        if (socket_id < 0 || socket_id >= 2)
        return -1;

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
/*
    snprintf(
        command,
        sizeof(command),
        "AT+NSORF=%d,%u",
        socket_id,
        (unsigned int)requested
    );

    */

    requested = 20;


esp_err_t err = ESP_FAIL;
bool leitura_ok = false;

for (int tentativa = 1; tentativa <= 3; tentativa++)
{
    memset(response, 0, sizeof(response));

    snprintf(
        command,
        sizeof(command),
        "AT+NSORF=%d,%u",
        socket_id,
        (unsigned int)requested
    );

    xSemaphoreTake(modem_mutex, portMAX_DELAY);

    err = modem_send_at(
        command,
        response,
        sizeof(response),
        MODEM_AT_TIMEOUT
    );

    xSemaphoreGive(modem_mutex);

    // O modem pode retornar 8002 temporariamente.
    if (strstr(response, "+CME ERROR:8002") != NULL)
    {
        ESP_LOGW(
            TAG,
            "NSORF ocupado. Tentativa %d/3",
            tentativa
        );

        if (tentativa < 3)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        return -1;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Erro na leitura NSORF: %s", response);
        return -1;
    }

    leitura_ok = true;
    break;
}

if (!leitura_ok)
    return -1;


char *p = strstr(response, "+NSORF:");

if (p == NULL)
{
    if (xSemaphoreTake(modem_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        socket_pending[socket_id] = 0;
        xSemaphoreGive(modem_mutex);
    }

    return 0;
}


int parsed = sscanf(
    p,
    "+NSORF: %d,%63[^,],%d,%d,%1400[^,],%d",
    &response_socket,
    remote_ip,
    &remote_port,
    &data_length,
    hex_data,
    &remaining
);

if (parsed != 6 || response_socket != socket_id)
{
    ESP_LOGE(TAG, "Resposta NSORF invalida: %s", p);
    return -1;
}

ESP_LOGI(
    TAG,
    "NSORF: socket=%d, IP=%s, porta=%d, bytes=%d, restantes=%d",
    response_socket,
    remote_ip,
    remote_port,
    data_length,
    remaining
);


    if (parsed != 6 || response_socket != socket_id)
    {
        ESP_LOGE(TAG, "Resposta NSORF invalida");
        return -1;
    }

    if (data_length <= 0)
    {
        if (xSemaphoreTake(modem_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            socket_pending[socket_id] = remaining;
            xSemaphoreGive(modem_mutex);
        }

        return 0;
    }

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

        if (xSemaphoreTake(modem_mutex, pdMS_TO_TICKS(1000)) == pdTRUE)
    {
        socket_pending[socket_id] = remaining;
        xSemaphoreGive(modem_mutex);
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
        if (socket_id < 0 || socket_id >= 2)
        return ESP_ERR_INVALID_ARG;
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

/* =========================================================
   PUBLICAR MQTT NATIVO PELO Y7080E
   ========================================================= */

esp_err_t modem_y7080e_mqtt_publish(
    const char *topic,
    const char *message
)
{
    if (topic == NULL || message == NULL || !modem_connected)
        return ESP_ERR_INVALID_ARG;

    size_t message_len = strlen(message);

    if (message_len == 0 || message_len > 1000)
        return ESP_ERR_INVALID_SIZE;

    char hex_data[2001];
    char command[2300];
    char response[MODEM_AT_RESPONSE_SIZE];

    /* Converte a mensagem para hexadecimal */
    for (size_t i = 0; i < message_len; i++)
    {
        snprintf(
            &hex_data[i * 2],
            3,
            "%02X",
            (unsigned char)message[i]
        );
    }

    hex_data[message_len * 2] = '\0';

    /* Monta comando MQTT PUB */
    snprintf(
        command,
        sizeof(command),
        "AT+MQPUB=0,\"%s\",0,0,0,%u,%s",
        topic,
        (unsigned int)message_len,
        hex_data
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
        ESP_LOGI(TAG, "MQTT publicado: %s", topic);
    }
    else
    {
        ESP_LOGE(TAG, "Falha ao publicar MQTT: %s", topic);
    }

    return err;
}

#include <stdio.h>
#include <string.h>

void modem_y7080e_manual_at_mode(void)
{
    char command[128];
    char response[MODEM_AT_RESPONSE_SIZE];

    ESP_LOGW(TAG, "================================");
    ESP_LOGW(TAG, "MODO AT MANUAL");
    ESP_LOGW(TAG, "Digite um comando AT no Monitor Serial");
    ESP_LOGW(TAG, "Digite sair para continuar");
    ESP_LOGW(TAG, "================================");

    while (1)
    {
        printf("\nAT> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
            continue;

        command[strcspn(command, "\r\n")] = '\0';

        if (strlen(command) == 0)
            continue;

        if (strcmp(command, "sair") == 0)
            break;

        memset(response, 0, sizeof(response));

        xSemaphoreTake(modem_mutex, portMAX_DELAY);

        esp_err_t err = modem_send_at(
            command,
            response,
            sizeof(response),
            MODEM_AT_TIMEOUT
        );

        xSemaphoreGive(modem_mutex);

        if (err == ESP_OK)
            printf("\nRESPOSTA:\n%s\n", response);
        else
            printf("\nERRO: %s\n", esp_err_to_name(err));
            
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGW(TAG, "Saindo do modo AT manual");
}
