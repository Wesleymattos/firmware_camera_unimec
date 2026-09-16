#include "event_camera.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_camera.h"

#include "nvs.h"

#include "sd_storage.h"


static const char *TAG = "EVENT_CAMERA";


#define EVENT_PHOTO_COUNT       10
#define EVENT_PHOTO_INTERVAL_MS 500

#define EVENT_NVS_NAMESPACE     "event_camera"
#define EVENT_NVS_KEY_COUNTER   "event_counter"


/*
 * Contador do último evento utilizado.
 *
 * Esse valor é carregado do NVS durante
 * a inicialização da câmera.
 */
static unsigned int next_event_id = 1;


/*
 * Carrega do NVS o número do próximo evento.
 */
static bool event_counter_load(void)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(
        EVENT_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel abrir NVS: %s",
            esp_err_to_name(ret)
        );

        return false;
    }


    uint32_t counter = 0;

    ret = nvs_get_u32(
        handle,
        EVENT_NVS_KEY_COUNTER,
        &counter
    );


    /*
     * A chave ainda nao existe.
     *
     * Isso acontece na primeira inicializacao
     * da camera.
     */
    if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        counter = 0;

        ESP_LOGI(
            TAG,
            "Contador de eventos ainda nao existe. Iniciando em 1."
        );
    }
    else if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro ao ler contador do NVS: %s",
            esp_err_to_name(ret)
        );

        nvs_close(handle);

        return false;
    }


    /*
     * O valor armazenado representa o ultimo
     * evento utilizado.
     *
     * Portanto o proximo sera +1.
     */
    next_event_id = counter + 1;


    nvs_close(handle);


    ESP_LOGI(
        TAG,
        "Proximo evento: %06u",
        next_event_id
    );


    return true;
}


/*
 * Salva no NVS o evento que acabou de ser utilizado.
 */
static bool event_counter_save(unsigned int event_id)
{
    nvs_handle_t handle;

    esp_err_t ret = nvs_open(
        EVENT_NVS_NAMESPACE,
        NVS_READWRITE,
        &handle
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel abrir NVS para salvar contador: %s",
            esp_err_to_name(ret)
        );

        return false;
    }


    ret = nvs_set_u32(
        handle,
        EVENT_NVS_KEY_COUNTER,
        event_id
    );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro ao salvar contador no NVS: %s",
            esp_err_to_name(ret)
        );

        nvs_close(handle);

        return false;
    }


    /*
     * Confirma a gravacao.
     */
    ret = nvs_commit(handle);


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Erro ao confirmar contador no NVS: %s",
            esp_err_to_name(ret)
        );

        nvs_close(handle);

        return false;
    }


    nvs_close(handle);


    ESP_LOGI(
        TAG,
        "Contador salvo no NVS: %06u",
        event_id
    );


    return true;
}


void event_camera_init(void)
{
    ESP_LOGI(
        TAG,
        "Modulo de captura de eventos inicializado"
    );


    ESP_LOGI(
        TAG,
        "Configuracao: %d fotos | intervalo: %d ms",
        EVENT_PHOTO_COUNT,
        EVENT_PHOTO_INTERVAL_MS
    );


    /*
     * Recupera o contador persistente.
     */
    if (!event_counter_load())
    {
        ESP_LOGE(
            TAG,
            "Falha ao carregar contador de eventos do NVS!"
        );

        /*
         * Mantemos 1 como valor de seguranca.
         */
        next_event_id = 1;
    }
}


bool event_camera_capture_event(void)
{
    /*
     * Guarda o ID deste evento.
     */
    unsigned int event_id = next_event_id;


    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "INICIANDO CAPTURA DO EVENTO"
    );

    ESP_LOGI(
        TAG,
        "Evento: %06u",
        event_id
    );

    ESP_LOGI(
        TAG,
        "%d fotos com intervalo de %d ms",
        EVENT_PHOTO_COUNT,
        EVENT_PHOTO_INTERVAL_MS
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    /*
     * Cria a pasta:
     *
     * /sdcard/events/event_000001/
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
    for (
        unsigned int i = 0;
        i < EVENT_PHOTO_COUNT;
        i++
    )
    {
        camera_fb_t *fb =
            esp_camera_fb_get();


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
        bool save_ok =
            sd_storage_save_photo(
                event_id,
                i + 1,
                fb->buf,
                fb->len
            );


        /*
         * Devolve o framebuffer
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
                pdMS_TO_TICKS(
                    EVENT_PHOTO_INTERVAL_MS
                )
            );
        }
    }


    /*
     * Todas as 10 fotos foram salvas.
     *
     * Agora podemos persistir o ID no NVS.
     */
    if (!event_counter_save(event_id))
    {
        ESP_LOGE(
            TAG,
            "ATENCAO: evento %06u foi salvo no SD, "
            "mas nao foi possivel salvar o contador no NVS!",
            event_id
        );

        return false;
    }


    /*
     * Prepara o próximo evento.
     */
    next_event_id++;


    ESP_LOGI(
        TAG,
        "========================================"
    );

    ESP_LOGI(
        TAG,
        "CAPTURA DO EVENTO CONCLUIDA"
    );

    ESP_LOGI(
        TAG,
        "Evento %06u",
        event_id
    );

    ESP_LOGI(
        TAG,
        "10 fotos salvas no cartao SD"
    );

    ESP_LOGI(
        TAG,
        "Proximo evento: %06u",
        next_event_id
    );

    ESP_LOGI(
        TAG,
        "========================================"
    );


    return true;
}