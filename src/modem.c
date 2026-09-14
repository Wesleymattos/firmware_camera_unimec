#include "modem.h"

#include "esp_log.h"

static const char *TAG = "MODEM";

void modem_init(void)
{
    /*
     * TODO:
     * Integrar aqui o código já funcional do SIM7080G.
     *
     * Arquitetura desejada:
     *
     * ESP32-S3
     *     |
     *    UART
     *     |
     * SIM7080G
     *     |
     *    PPP
     *     |
     *   TCP/IP
     */
    ESP_LOGI(TAG, "Modem SIM7080G inicializado (STUB)");
}

bool modem_is_connected(void)
{
    /*
     * TODO:
     * Retornar o estado real da interface PPP.
     */
    return false;
}

void modem_process(void)
{
    /*
     * TODO:
     * Manutenção da conexão PPP/modem.
     */
}
