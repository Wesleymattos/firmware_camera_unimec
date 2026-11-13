
#include "teste_modem.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_vfs_dev.h"
#include "driver/uart_vfs.h"

#include <unistd.h>
#include <fcntl.h>

#define TESTE_MODEM_UART       UART_NUM_1
#define TESTE_MODEM_TX_GPIO    1
#define TESTE_MODEM_RX_GPIO    41
#define TESTE_MODEM_BAUDRATE   9600
#define TESTE_MODEM_BUF_SIZE   1024

#define TESTE_MODEM_RESET_GPIO GPIO_NUM_47

static const char *TAG = "TESTE_MODEM";

static void teste_modem_reset(void)
{
    ESP_LOGI(TAG, "Acionando RESET do Y7080E");

    gpio_config_t reset_config = {
        .pin_bit_mask = (1ULL << TESTE_MODEM_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&reset_config));

    gpio_set_level(TESTE_MODEM_RESET_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(TESTE_MODEM_RESET_GPIO, 0);

    ESP_LOGI(TAG, "Aguardando inicializacao do modem...");
    vTaskDelay(pdMS_TO_TICKS(3000));
}

void teste_modem(void)
{
    esp_err_t err;

    teste_modem_reset();

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
        ESP_LOGE(TAG, "Erro UART: %s", esp_err_to_name(err));
        return;
    }

    uart_param_config(TESTE_MODEM_UART, &uart_config);

    uart_set_pin(
        TESTE_MODEM_UART,
        TESTE_MODEM_TX_GPIO,
        TESTE_MODEM_RX_GPIO,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    uart_flush_input(TESTE_MODEM_UART);

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "TERMINAL AT INTERATIVO - Y7080E");
    ESP_LOGI(TAG, "UART1 | TX GPIO%d | RX GPIO%d | %d baud",
             TESTE_MODEM_TX_GPIO,
             TESTE_MODEM_RX_GPIO,
             TESTE_MODEM_BAUDRATE);
    ESP_LOGI(TAG, "Digite comandos AT no Monitor Serial.");
    ESP_LOGI(TAG, "Digite sair para encerrar.");
    ESP_LOGI(TAG, "======================================");

    char comando[256];
    uint8_t rx_buffer[TESTE_MODEM_BUF_SIZE];

    int posicao = 0;
    uint8_t caractere;

    while (1)
    {
        // Recebe caracteres do Monitor Serial USB
int recebido = read(STDIN_FILENO, &caractere, 1);

        if (recebido > 0)
        {
            if (caractere == '\r' || caractere == '\n')
            {
                if (posicao > 0)
                {
                    comando[posicao] = '\0';

                    if (strcmp(comando, "sair") == 0)
                        break;

                    ESP_LOGI(TAG, "TX: %s", comando);

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

                    posicao = 0;
                }
            }
            else if (posicao < sizeof(comando) - 1)
            {
                comando[posicao++] = (char)caractere;
            }
        }

        // Lê a resposta do modem
        int len = uart_read_bytes(
            TESTE_MODEM_UART,
            rx_buffer,
            sizeof(rx_buffer) - 1,
            pdMS_TO_TICKS(20)
        );

        if (len > 0)
        {
            rx_buffer[len] = '\0';
            printf("%s", (char *)rx_buffer);
            fflush(stdout);
        }
    }

    uart_driver_delete(TESTE_MODEM_UART);

    ESP_LOGI(TAG, "Terminal AT encerrado. UART1 liberada.");
}
