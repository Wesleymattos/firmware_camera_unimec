#include "detector.h"

#include "esp_log.h"
#include "vl53l0x.h"

static const char *TAG = "DETECTOR";

#define MOVIMENTO_MINIMO_MM           150
#define LEITURAS_MOVIMENTO_CONFIRMAR  3

// VL53L0X usa valores próximos de 8191 mm para indicar
// que a distância está fora do alcance.
#define DISTANCIA_FORA_DO_ALCANCE_MM  8190

static uint16_t distancia_anterior_mm = 0;
static uint8_t leituras_movimento = 0;
static bool evento_disparado = false;

void detector_init(void)
{
    distancia_anterior_mm = 0;
    leituras_movimento = 0;
    evento_disparado = false;

    ESP_LOGI(
        TAG,
        "Detector inicializado"
    );

    ESP_LOGI(
        TAG,
        "Movimento minimo: %d mm",
        MOVIMENTO_MINIMO_MM
    );

    ESP_LOGI(
        TAG,
        "Confirmacao: %d leituras consecutivas",
        LEITURAS_MOVIMENTO_CONFIRMAR
    );
}

void detector_reset(void)
{
    distancia_anterior_mm = 0;
    leituras_movimento = 0;
    evento_disparado = false;

    ESP_LOGI(
        TAG,
        "Detector resetado - pronto para nova deteccao"
    );
}

bool detector_pessoa_detectada(void)
{
    /*
     * Depois que um evento foi disparado,
     * o detector permanece bloqueado.
     *
     * Somente detector_reset() pode libera-lo novamente.
     */
    if (evento_disparado)
    {
        return false;
    }

    uint16_t distancia_atual_mm = 0;

    if (!vl53l0x_read_distance(&distancia_atual_mm))
    {
        ESP_LOGW(
            TAG,
            "Falha na leitura do VL53L0X"
        );

        return false;
    }

    /*
     * Ignora leituras fora do alcance.
     *
     * 8190/8191 mm nao representam uma distancia real.
     * Portanto, elas nao podem:
     * - gerar movimento;
     * - alterar a distancia de referencia;
     * - interferir na comparacao seguinte.
     */
    if (distancia_atual_mm >= DISTANCIA_FORA_DO_ALCANCE_MM)
    {
        ESP_LOGW(
            TAG,
            "Distancia fora do alcance: %u mm - ignorando",
            distancia_atual_mm
        );

        return false;
    }

    /*
     * Primeira leitura valida:
     * estabelece a referencia inicial.
     */
    if (distancia_anterior_mm == 0)
    {
        distancia_anterior_mm = distancia_atual_mm;

        ESP_LOGI(
            TAG,
            "Referencia inicial: %u mm",
            distancia_atual_mm
        );

        return false;
    }

    uint16_t diferenca_mm;

    if (distancia_atual_mm > distancia_anterior_mm)
    {
        diferenca_mm =
            distancia_atual_mm -
            distancia_anterior_mm;
    }
    else
    {
        diferenca_mm =
            distancia_anterior_mm -
            distancia_atual_mm;
    }

    ESP_LOGI(
        TAG,
        "Distancia: %u mm | Anterior: %u mm | Variacao: %u mm",
        distancia_atual_mm,
        distancia_anterior_mm,
        diferenca_mm
    );

    /*
     * Detecta uma variacao significativa de distancia.
     */
    if (diferenca_mm >= MOVIMENTO_MINIMO_MM)
    {
        leituras_movimento++;

        ESP_LOGI(
            TAG,
            "Movimento detectado: %u/%u",
            leituras_movimento,
            LEITURAS_MOVIMENTO_CONFIRMAR
        );
    }
    else
    {
        leituras_movimento = 0;
    }

    /*
     * Atualiza a referencia somente com uma leitura valida.
     */
    distancia_anterior_mm = distancia_atual_mm;

    /*
     * Confirma o movimento somente depois de
     * 3 leituras consecutivas.
     */
    if (leituras_movimento >= LEITURAS_MOVIMENTO_CONFIRMAR)
    {
        evento_disparado = true;
        leituras_movimento = 0;

        ESP_LOGW(
            TAG,
            ">>> MOVIMENTO CONFIRMADO - DISPARANDO EVENTO <<<"
        );

        return true;
    }

    return false;
}