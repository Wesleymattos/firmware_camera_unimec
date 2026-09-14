#include "detector.h"

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DETECTOR";

static bool evento_teste_disparado = false;
static int64_t tempo_inicio_us = 0;

void detector_init(void)
{
    tempo_inicio_us = esp_timer_get_time();

    ESP_LOGI(
        TAG,
        "Detector inicializado (MODO TESTE)"
    );

    ESP_LOGI(
        TAG,
        "Pessoa sera simulada em 5 segundos..."
    );
}

bool detector_pessoa_detectada(void)
{
    if (evento_teste_disparado)
    {
        return false;
    }

    int64_t agora_us = esp_timer_get_time();

    int64_t tempo_decorrido_us =
        agora_us - tempo_inicio_us;

    if (tempo_decorrido_us >= 5000000)
    {
        evento_teste_disparado = true;

        ESP_LOGW(
            TAG,
            ">>> PESSOA DETECTADA - SIMULACAO <<<"
        );

        return true;
    }

    return false;
}