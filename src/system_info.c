#include "system_info.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "SYSTEM";


void system_info_init(void)
{
    ESP_LOGI(
        TAG,
        "======================================"
    );

    ESP_LOGI(
        TAG,
        "      UNIMEC SECURITY CAMERA"
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );


    esp_chip_info_t chip_info;

    esp_chip_info(&chip_info);

    ESP_LOGI(
        TAG,
        "CPU cores: %d",
        chip_info.cores
    );

    ESP_LOGI(
        TAG,
        "Revision: %d",
        chip_info.revision
    );


    uint32_t flash_size = 0;

    esp_err_t err =
        esp_flash_get_size(
            esp_flash_default_chip,
            &flash_size
        );

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "Flash: %lu MB",
            (unsigned long)(
                flash_size /
                (1024 * 1024)
            )
        );
    }


    size_t psram_total =
        heap_caps_get_total_size(
            MALLOC_CAP_SPIRAM
        );

    size_t psram_free =
        heap_caps_get_free_size(
            MALLOC_CAP_SPIRAM
        );


    ESP_LOGI(
        TAG,
        "PSRAM total: %lu bytes",
        (unsigned long)psram_total
    );

    ESP_LOGI(
        TAG,
        "PSRAM livre: %lu bytes",
        (unsigned long)psram_free
    );


    if (psram_total > 0)
    {
        ESP_LOGI(
            TAG,
            "PSRAM OK!"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "PSRAM NAO DETECTADA!"
        );
    }
}


void system_info_log(void)
{
    ESP_LOGI(
        TAG,
        "Sistema funcionando | PSRAM livre: %lu bytes",
        (unsigned long)
            heap_caps_get_free_size(
                MALLOC_CAP_SPIRAM
            )
    );
}