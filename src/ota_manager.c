
#include "ota_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "modem.h"

static const char *TAG = "OTA";

#define OTA_TASK_STACK_SIZE 8192
#define OTA_TASK_PRIORITY   5

static TaskHandle_t ota_task_handle = NULL;

static bool ota_running = false;

static char ota_firmware_url[512];

/*
 * Tarefa OTA.
 *
 * O download HTTPS será implementado na próxima etapa.
 */
static void ota_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Tarefa OTA iniciada");

    ESP_LOGI(
        TAG,
        "URL recebida: %s",
        ota_firmware_url
    );

    /*
     * Aguarda a conexão PPP.
     */
    while (!modem_is_connected())
    {
        ESP_LOGW(
            TAG,
            "Aguardando conexão do modem..."
        );

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(
        TAG,
        "Conexão PPP disponível"
    );

    /*
     * Próxima etapa:
     * download HTTPS e gravação da partição OTA.
     */

    ota_running = false;
    ota_task_handle = NULL;

    vTaskDelete(NULL);
}

/*
 * Inicializa o módulo OTA.
 */
esp_err_t ota_manager_init(void)
{
    ESP_LOGI(TAG, "Módulo OTA inicializado");

    return ESP_OK;
}

/*
 * Solicita uma atualização.
 */
esp_err_t ota_manager_start(const char *firmware_url)
{
    if (firmware_url == NULL)
    {
        ESP_LOGE(TAG, "URL OTA inválida");

        return ESP_ERR_INVALID_ARG;
    }

    if (ota_running)
    {
        ESP_LOGW(TAG, "OTA já está em andamento");

        return ESP_ERR_INVALID_STATE;
    }

    size_t url_len = strlen(firmware_url);

    if (url_len == 0 || url_len >= sizeof(ota_firmware_url))
    {
        ESP_LOGE(TAG, "URL OTA muito longa");

        return ESP_ERR_INVALID_SIZE;
    }

    if (strncmp(firmware_url, "https://", 8) != 0)
    {
        ESP_LOGE(TAG, "OTA exige URL HTTPS");

        return ESP_ERR_INVALID_ARG;
    }

    strcpy(
        ota_firmware_url,
        firmware_url
    );

    ota_running = true;

    BaseType_t result = xTaskCreate(
        ota_task,
        "ota_task",
        OTA_TASK_STACK_SIZE,
        NULL,
        OTA_TASK_PRIORITY,
        &ota_task_handle
    );

    if (result != pdPASS)
    {
        ota_running = false;
        ota_task_handle = NULL;

        ESP_LOGE(TAG, "Falha ao criar tarefa OTA");

        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Solicitação OTA aceita");

    return ESP_OK;
}

/*
 * Consulta se o OTA está em andamento.
 */
bool ota_manager_is_running(void)
{
    return ota_running;
}
