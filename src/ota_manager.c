
#include "ota_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"

#include "modem.h"

static const char *TAG = "OTA";

#define OTA_TASK_STACK_SIZE 8192
#define OTA_TASK_PRIORITY   5

static TaskHandle_t ota_task_handle = NULL;
static volatile bool ota_running = false;

static char ota_firmware_url[512];

/*
 * Tarefa responsável pelo download e instalação do firmware.
 */
static void ota_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Tarefa OTA iniciada");

    /*
     * Aguarda a conexão PPP.
     */
    while (!modem_is_connected())
    {
        ESP_LOGW(TAG, "Aguardando conexão do modem...");

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Conexão PPP disponível");
    ESP_LOGI(TAG, "Iniciando download HTTPS");

    esp_http_client_config_t http_config = {
        .url = ota_firmware_url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t err = esp_https_ota(&ota_config);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA concluído com sucesso!");
        ESP_LOGI(TAG, "Reiniciando ESP32...");

        vTaskDelay(pdMS_TO_TICKS(1000));

        esp_restart();
    }

    ESP_LOGE(
        TAG,
        "Falha no OTA: %s",
        esp_err_to_name(err)
    );

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

    strcpy(ota_firmware_url, firmware_url);

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
