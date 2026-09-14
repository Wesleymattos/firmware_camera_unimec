#include "event_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_camera.h"

#include "sd_storage.h"

static const char *TAG = "EVENT_CAMERA";

#define EVENT_PHOTO_COUNT       10
#define EVENT_PHOTO_INTERVAL_MS 500

/*
 * ID do próximo evento.
 *
 * Por enquanto começa em 1 a cada boot.
 * Depois vamos fazer isso persistente no SD.
 */
static unsigned int next_event_id = 1;

void event_camera_init(void)
{
    ESP_LOGI(TAG, "Modulo de captura de eventos inicializado");

    ESP_LOGI(
        TAG,
        "Configuracao: %d fotos | intervalo: %d ms",
        EVENT_PHOTO_COUNT,
        EVENT_PHOTO_INTERVAL_MS
    );
}

bool event_camera_capture_event(void)
{
    unsigned int event_id = next_event_id++;

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "INICIANDO CAPTURA DO EVENTO");
    ESP_LOGI(TAG, "Evento: %06u", event_id);
    ESP_LOGI(
        TAG,
        "%d fotos com intervalo de %d ms",
        EVENT_PHOTO_COUNT,
        EVENT_PHOTO_INTERVAL_MS
    );
    ESP_LOGI(TAG, "========================================");

    /*
     * Cria:
     *
     * /sdcard/events/event_000001/
     *
     * e o status.txt com PENDING.
     */
    if (!sd_storage_create_event(event_id))
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar o evento %06u no SD!",
            event_id
        );

        return false;
    }

    /*
     * Captura e salva as 10 fotos.
     */
    for (unsigned int i = 0; i < EVENT_PHOTO_COUNT; i++)
    {
        camera_fb_t *fb = esp_camera_fb_get();

        if (fb == NULL)
        {
            ESP_LOGE(
                TAG,
                "Falha ao capturar foto %u",
                i + 1
            );

            return false;
        }

        ESP_LOGI(
            TAG,
            "Foto %u/%d capturada: %ux%u | %u bytes",
            i + 1,
            EVENT_PHOTO_COUNT,
            fb->width,
            fb->height,
            fb->len
        );

        /*
         * Salva a imagem JPEG no cartão SD.
         */
        bool save_ok = sd_storage_save_photo(
            event_id,
            i + 1,
            fb->buf,
            fb->len
        );

        /*
         * Depois de salvar, devolvemos o framebuffer
         * para o driver da câmera.
         */
        esp_camera_fb_return(fb);

        if (!save_ok)
        {
            ESP_LOGE(
                TAG,
                "Falha ao salvar foto %u/%d no SD!",
                i + 1,
                EVENT_PHOTO_COUNT
            );

            return false;
        }

        ESP_LOGI(
            TAG,
            "Foto %u/%d salva no SD com sucesso!",
            i + 1,
            EVENT_PHOTO_COUNT
        );

        if (i < EVENT_PHOTO_COUNT - 1)
        {
            vTaskDelay(
                pdMS_TO_TICKS(EVENT_PHOTO_INTERVAL_MS)
            );
        }
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "CAPTURA DO EVENTO CONCLUIDA");
    ESP_LOGI(TAG, "Evento %06u", event_id);
    ESP_LOGI(TAG, "10 fotos salvas no cartao SD");
    ESP_LOGI(TAG, "========================================");

    return true;
}