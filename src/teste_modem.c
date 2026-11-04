#include "teste_modem.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TESTE_MODEM_UART       UART_NUM_1
#define TESTE_MODEM_TX_GPIO    1
#define TESTE_MODEM_RX_GPIO    41
#define TESTE_MODEM_BAUDRATE   115200
#define TESTE_MODEM_BUF_SIZE   1024

static const char *TAG = "TESTE_MODEM";

void teste_modem(void)
{
    esp_err_t err;

    uart_config_t uart_config = {
        .baud_rate = TESTE_MODEM_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    err = uart_driver_install(
        TESTE_MODEM_UART,
        TESTE_MODEM_BUF_SIZE * 2,
        TESTE_MODEM_BUF_SIZE * 2,
        0,
        NULL,
        0
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha uart_driver_install: %s",
                 esp_err_to_name(err));
        return;
    }

    err = uart_param_config(
        TESTE_MODEM_UART,
        &uart_config
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha uart_param_config: %s",
                 esp_err_to_name(err));
        uart_driver_delete(TESTE_MODEM_UART);
        return;
    }

    err = uart_set_pin(
        TESTE_MODEM_UART,
        TESTE_MODEM_TX_GPIO,
        TESTE_MODEM_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha uart_set_pin: %s",
                 esp_err_to_name(err));
        uart_driver_delete(TESTE_MODEM_UART);
        return;
    }

    uart_flush_input(TESTE_MODEM_UART);

    uint8_t rx_buffer[TESTE_MODEM_BUF_SIZE];

    const char *testes[] = {
        "AT",
        "ATI",
        "AT+CGMR",
        "AT+CPIN?",
        "AT+CCID",
        "AT+CSQ",
        "AT+CESQ",
        "AT+CEREG?",
        "AT+CGREG?",
        "AT+CREG?",
        "AT+COPS?",
        "AT+CGATT?",
        "AT+CGDCONT?",
        "AT+CGACT?",
        "AT+CGPADDR",
        "AT+CGCONTRDP"
    };

    const int quantidade_testes =
        sizeof(testes) / sizeof(testes[0]);

    ESP_LOGI(TAG, "Iniciando sequencia de testes SIM7080G");
    ESP_LOGI(TAG, "TX GPIO%d | RX GPIO%d | %d baud",
             TESTE_MODEM_TX_GPIO,
             TESTE_MODEM_RX_GPIO,
             TESTE_MODEM_BAUDRATE);

    for (int i = 0; i < quantidade_testes; i++)
    {
        const char *comando = testes[i];

        printf("\n");
        printf("========================================\n");
        printf("TESTE %d/%d: %s\n",
               i + 1,
               quantidade_testes,
               comando);
        printf("========================================\n");

        uart_flush_input(TESTE_MODEM_UART);

        uart_write_bytes(
            TESTE_MODEM_UART,
            comando,
            strlen(comando)
        );

        uart_write_bytes(
            TESTE_MODEM_UART,
            "\r\n",
            2
        );

        uart_wait_tx_done(
            TESTE_MODEM_UART,
            pdMS_TO_TICKS(1000)
        );

        int total_bytes = 0;

        TickType_t inicio = xTaskGetTickCount();

        while ((xTaskGetTickCount() - inicio) <
               pdMS_TO_TICKS(2500))
        {
            int len = uart_read_bytes(
                TESTE_MODEM_UART,
                rx_buffer,
                sizeof(rx_buffer) - 1,
                pdMS_TO_TICKS(200)
            );

            if (len > 0)
            {
                rx_buffer[len] = '\0';

                printf("%s", (char *)rx_buffer);
                fflush(stdout);

                total_bytes += len;
            }
        }

        if (total_bytes == 0) {
            printf("SEM RESPOSTA DO MODEM\n");
        }

        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Sequencia de testes finalizada");

    /*
     * Libera UART1 para o esp_modem.
     */
    err = uart_driver_delete(TESTE_MODEM_UART);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao liberar UART1: %s",
                 esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "UART1 liberada");
}