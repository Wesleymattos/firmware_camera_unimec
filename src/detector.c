#include "detector.h"

#include "esp_log.h"
#include "hc_sr501.h"


static const char *TAG = "DETECTOR";



static bool evento_disparado = false;

void detector_init(void)
{

    evento_disparado = false;

    ESP_LOGI(
        TAG,
        "Detector inicializado"
    );



}

void detector_reset(void)
{

    evento_disparado = false;

    ESP_LOGI(
        TAG,
        "Detector resetado - pronto para nova deteccao"
    );
}

bool detector_pessoa_detectada(void)
{
    if (evento_disparado)
    {
        return false;
    }

    if (hc_sr501_detected())
    {
        evento_disparado = true;

        ESP_LOGW(TAG, "PRESENCA DETECTADA PELO HC-SR501");

        return true;
    }

    return false;
}