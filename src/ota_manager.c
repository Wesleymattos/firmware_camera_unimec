
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

#include "led_status.h"

#include "mqtt_manager.h"

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
    led_status_ota_start();


    mqtt_manager_publish_ota_progress(
    "starting",
    0,
    0,
    0
);

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

    ESP_LOGI(TAG, "URL recebida: [%s]", ota_firmware_url);
ESP_LOGI(TAG, "Tamanho da URL: %d", (int)strlen(ota_firmware_url));

esp_http_client_config_t http_config = {
    .url = ota_firmware_url,
    .timeout_ms = 60000,
    .keep_alive_enable = true,
    .crt_bundle_attach = esp_crt_bundle_attach,

    .buffer_size = 4096,
    .buffer_size_tx = 2048,
};

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };



    esp_https_ota_handle_t ota_handle = NULL;
int image_size = 0;
int image_read = 0;
int last_progress = -1;
int progress = 0;

/* Início da sessão OTA */
esp_err_t err = esp_https_ota_begin(
    &ota_config,
    &ota_handle
);

if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Falha ao iniciar OTA: %s", esp_err_to_name(err));

    mqtt_manager_publish_ota_progress(
        "error",
        0,
        0,
        0
    );

    ota_running = false;
    ota_task_handle = NULL;
    led_status_relay_off();
    vTaskDelete(NULL);
    return;
}

/* Obtém o tamanho total da imagem */
image_size = esp_https_ota_get_image_size(ota_handle);

ESP_LOGI(
    TAG,
    "Tamanho da imagem: %d bytes",
    image_size
);



    /* Download da imagem com acompanhamento do progresso */
while (1)
{
    err = esp_https_ota_perform(ota_handle);

    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
    {
        break;
    }

    image_read = esp_https_ota_get_image_len_read(ota_handle);

    if (image_size > 0 && image_read >= 0)
    {
        progress = (image_read * 100) / image_size;

        if (progress != last_progress)
        {
            last_progress = progress;

            ESP_LOGI(
                TAG,
                "OTA: %d%% (%d / %d bytes)",
                progress,
                image_read,
                image_size
            );

            mqtt_manager_publish_ota_progress(
                "downloading",
                progress,
                image_read,
                image_size
            );
        }
    }
}

/* Verifica se o download terminou corretamente */
if (err == ESP_OK &&
    esp_https_ota_is_complete_data_received(ota_handle))
{
    ESP_LOGI(TAG, "Download completo. Validando firmware...");

    mqtt_manager_publish_ota_progress(
        "verifying",
        100,
        image_read,
        image_size
    );

    err = esp_https_ota_finish(ota_handle);
}
else
{
    ESP_LOGE(TAG, "Download incompleto ou falhou");

    esp_https_ota_abort(ota_handle);
    err = ESP_FAIL;
}

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA concluído com sucesso!");

        mqtt_manager_publish_ota_progress(
    "completed",
    100,
    image_size,
    image_size
);
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

/* Retorna o LED ao estado de relé desligado */
led_status_relay_off();

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
