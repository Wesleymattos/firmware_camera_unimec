#include "event_manager.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <time.h>

#include "relay.h"
#include "event_camera.h"
#include "mqtt_manager.h"
#include "cloud_upload.h"
#include "detector.h"

static const char *TAG = "EVENT";

static bool alarme_ativo = false;

/*
 * TESTE PROVISORIO
 *
 * O alarme sera desligado automaticamente
 * apos 10 segundos.
 */
#define TEMPO_ALARME_TESTE_MS 10000

static int64_t tempo_alarme_inicio_us = 0;


/* ============================================================
 * INICIALIZACAO
 * ============================================================ */

void event_manager_init(void)
{
    alarme_ativo = false;
    tempo_alarme_inicio_us = 0;

    ESP_LOGI(
        TAG,
        "Event Manager inicializado"
    );
}


/* ============================================================
 * ACIONAMENTO MANUAL
 * ============================================================ */

void event_manager_acionar(void)
{
    if (alarme_ativo)
    {
        ESP_LOGW(
            TAG,
            "Acionar solicitado, mas alarme ja esta ativo"
        );

        return;
    }


    ESP_LOGW(
        TAG,
        "========================================"
    );

    ESP_LOGW(
        TAG,
        "       ALARME ACIONADO MANUALMENTE"
    );

    ESP_LOGW(
        TAG,
        "========================================"
    );


    alarme_ativo = true;

    tempo_alarme_inicio_us = esp_timer_get_time();

    relay_on();
}


/* ============================================================
 * PESSOA DETECTADA
 * ============================================================ */

void event_manager_pessoa_detectada(void)
{
    if (alarme_ativo)
    {
        ESP_LOGW(
            TAG,
            "Pessoa detectada, mas alarme ja esta ativo"
        );

        return;
    }


    ESP_LOGW(
        TAG,
        "========================================"
    );

    ESP_LOGW(
        TAG,
        "        PESSOA DETECTADA!"
    );

    ESP_LOGW(
        TAG,
        "========================================"
    );


    alarme_ativo = true;

    /*
     * Inicia contador do alarme.
     */
    tempo_alarme_inicio_us = esp_timer_get_time();


    time_t event_timestamp;

    time(&event_timestamp);


    /* --------------------------------------------------------
     * Liga rele
     * -------------------------------------------------------- */

    relay_on();


    /* --------------------------------------------------------
     * Envia alerta MQTT
     * -------------------------------------------------------- */

    if (mqtt_manager_publish_alert())
    {
        ESP_LOGI(
            TAG,
            "Alerta enviado para o servidor MQTT"
        );
    }
    else
    {
        ESP_LOGW(
            TAG,
            "Nao foi possivel enviar alerta MQTT"
        );
    }


    /* --------------------------------------------------------
     * Captura evento
     * -------------------------------------------------------- */

    unsigned int event_id = 0;


    bool captura_ok =
        event_camera_capture_event(
            &event_id,
            event_timestamp
        );


    if (!captura_ok)
    {
        ESP_LOGE(
            TAG,
            "Falha durante a captura das fotos"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Captura das 10 fotos concluida com sucesso"
    );


    ESP_LOGI(
        TAG,
        "Evento capturado: %06u",
        event_id
    );


    /* --------------------------------------------------------
     * Inicia upload em task separada
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Iniciando task de upload do evento %06u...",
        event_id
    );


    cloud_upload_start_event(
        event_id
    );


    ESP_LOGI(
        TAG,
        "Task de upload iniciada para o evento %06u",
        event_id
    );
}


/* ============================================================
 * PROCESSAMENTO
 * ============================================================ */

void event_manager_process(void)
{
    /*
     * TESTE PROVISORIO
     *
     * Desliga automaticamente o alarme
     * apos 10 segundos.
     */

    if (!alarme_ativo)
    {
        return;
    }


    int64_t agora_us = esp_timer_get_time();

    int64_t tempo_decorrido_us =
        agora_us - tempo_alarme_inicio_us;


    if (tempo_decorrido_us >=
        ((int64_t)TEMPO_ALARME_TESTE_MS * 1000))
    {
        ESP_LOGW(
            TAG,
            "========================================"
        );

        ESP_LOGW(
            TAG,
            " ALARME TESTE: 10 SEGUNDOS COMPLETOS"
        );

        ESP_LOGW(
            TAG,
            " DESARMANDO AUTOMATICAMENTE"
        );

        ESP_LOGW(
            TAG,
            "========================================"
        );


        relay_off();

        alarme_ativo = false;

        tempo_alarme_inicio_us = 0;


        /*
         * Libera o detector para uma nova deteccao.
         */
        detector_reset();
    }
}


/* ============================================================
 * DESARMAR
 * ============================================================ */

void event_manager_desarmar(void)
{
    if (!alarme_ativo)
    {
        ESP_LOGW(
            TAG,
            "Desarmar solicitado, mas alarme ja esta desligado"
        );

        relay_off();

        detector_reset();

        return;
    }


    ESP_LOGW(
        TAG,
        "========================================"
    );

    ESP_LOGW(
        TAG,
        "          ALARME DESARMADO"
    );

    ESP_LOGW(
        TAG,
        "========================================"
    );


    relay_off();

    alarme_ativo = false;

    tempo_alarme_inicio_us = 0;

    /*
     * Libera o detector para uma nova deteccao.
     */
    detector_reset();
}