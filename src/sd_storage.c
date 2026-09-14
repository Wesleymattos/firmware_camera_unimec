#include "sd_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "SD_STORAGE";

/*
 * Ponto de montagem do cartão.
 */
#define SD_MOUNT_POINT "/sdcard"

/*
 * Pinagem do SD integrado da Freenove ESP32-S3-WROOM.
 *
 * SDMMC 1-bit:
 *
 * CMD  -> GPIO38
 * CLK  -> GPIO39
 * D0   -> GPIO40
 */
#define SD_PIN_CMD GPIO_NUM_38
#define SD_PIN_CLK GPIO_NUM_39
#define SD_PIN_D0  GPIO_NUM_40

/*
 * Diretório onde ficarão os eventos.
 */
#define EVENTS_DIR "/sdcard/events"

static sdmmc_card_t *sd_card = NULL;
static bool sd_mounted = false;

void sd_storage_init(void)
{
    ESP_LOGI(TAG, "######## SD_STORAGE_INIT ENTROU ########");

    ESP_LOGI(TAG, "Inicializando cartao SD...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 20,
        .allocation_unit_size = 16 * 1024
    };

    // resto permanece igual...

    /*
     * Configuração do controlador SDMMC.
     */
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    /*
     * Estamos usando apenas uma linha de dados.
     */
    sdmmc_slot_config_t slot_config =
        SDMMC_SLOT_CONFIG_DEFAULT();

    slot_config.width = 1;

    /*
     * Pinagem específica da placa.
     */
    slot_config.clk = SD_PIN_CLK;
    slot_config.cmd = SD_PIN_CMD;
    slot_config.d0  = SD_PIN_D0;

    /*
     * Ativa pull-ups internos.
     *
     * IMPORTANTE:
     * A documentação do ESP-IDF ressalta que os pull-ups
     * internos não substituem os externos de 10 kΩ quando
     * necessários pelo hardware.
     */
    slot_config.flags |=
        SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Montando FATFS...");

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &sd_card
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao montar cartao SD: %s",
            esp_err_to_name(ret)
        );

        if (ret == ESP_FAIL)
        {
            ESP_LOGE(
                TAG,
                "Falha no FATFS ou sistema de arquivos."
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Falha ao inicializar o SDMMC."
            );

            ESP_LOGE(
                TAG,
                "Verifique cartao, formato e conexoes."
            );
        }

        sd_mounted = false;
        return;
    }

    sd_mounted = true;

    ESP_LOGI(
        TAG,
        "Cartao SD montado com sucesso!"
    );

    /*
     * Mostra informações do cartão.
     */
    sdmmc_card_print_info(stdout, sd_card);

    /*
     * Cria o diretório principal dos eventos.
     */
    if (mkdir(EVENTS_DIR, 0775) != 0)
    {
        if (errno == EEXIST)
        {
            ESP_LOGI(
                TAG,
                "Diretorio %s ja existe",
                EVENTS_DIR
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Falha ao criar %s: errno=%d",
                EVENTS_DIR,
                errno
            );
        }
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Diretorio %s criado",
            EVENTS_DIR
        );
    }
}

bool sd_storage_create_event(unsigned int event_id)
{
    if (!sd_mounted)
    {
        ESP_LOGE(
            TAG,
            "SD nao esta montado!"
        );

        return false;
    }

    char event_path[64];

    snprintf(
        event_path,
        sizeof(event_path),
        EVENTS_DIR "/event_%06u",
        event_id
    );

    /*
     * Cria o diretório do evento.
     */
    if (mkdir(event_path, 0775) != 0)
    {
        if (errno == EEXIST)
        {
            ESP_LOGW(
                TAG,
                "Evento ja existe: %s",
                event_path
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Falha ao criar evento: %s | errno=%d",
                event_path,
                errno
            );

            return false;
        }
    }

    /*
     * Cria o status inicial.
     *
     * Futuramente:
     *
     * PENDING
     *
     * significa que ainda precisa ser enviado para a nuvem.
     */
    char status_path[80];

    snprintf(
        status_path,
        sizeof(status_path),
        "%s/status.txt",
        event_path
    );

    FILE *file = fopen(
        status_path,
        "w"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar status.txt"
        );

        return false;
    }

    fprintf(
        file,
        "PENDING\n"
    );

    fclose(file);

    ESP_LOGI(
        TAG,
        "Evento criado: %s",
        event_path
    );

    return true;
}

bool sd_storage_save_photo(
    unsigned int event_id,
    unsigned int photo_number,
    const unsigned char *data,
    unsigned int size
)
{
    if (!sd_mounted)
    {
        ESP_LOGE(
            TAG,
            "SD nao esta montado!"
        );

        return false;
    }

    if (data == NULL || size == 0)
    {
        ESP_LOGE(
            TAG,
            "Dados da foto invalidos!"
        );

        return false;
    }

    char event_path[64];

    snprintf(
        event_path,
        sizeof(event_path),
        EVENTS_DIR "/event_%06u",
        event_id
    );

    char photo_path[100];

    snprintf(
        photo_path,
        sizeof(photo_path),
        "%s/photo_%02u.jpg",
        event_path,
        photo_number
    );

    ESP_LOGI(
        TAG,
        "Gravando foto: %s",
        photo_path
    );

    FILE *file = fopen(
        photo_path,
        "wb"
    );

    if (file == NULL)
    {
        ESP_LOGE(
            TAG,
            "Falha ao abrir arquivo: %s | errno=%d",
            photo_path,
            errno
        );

        return false;
    }

    size_t written = fwrite(
        data,
        1,
        size,
        file
    );

    fclose(file);

    if (written != size)
    {
        ESP_LOGE(
            TAG,
            "Falha na gravacao: %u/%u bytes",
            (unsigned int)written,
            size
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Foto salva: %u bytes",
        (unsigned int)written
    );

    return true;
}

bool sd_storage_has_pending(void)
{
    if (!sd_mounted)
    {
        return false;
    }

    /*
     * TODO:
     *
     * Percorrer /sdcard/events/
     * e procurar eventos com:
     *
     * status.txt = PENDING
     *
     * Isso será implementado na fila de upload.
     */

    return false;
}