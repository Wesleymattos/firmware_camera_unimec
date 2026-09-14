#include "event_manager.h"

#include "esp_log.h"

#include <stdbool.h>

#include "relay.h"
#include "event_camera.h"
#include "mqtt_manager.h"

static const char *TAG = "EVENT";

static bool alarme_ativo = false;


void event_manager_init(void)
{
    alarme_ativo = false;

    ESP_LOGI(
        TAG,
        "Event Manager inicializado"
    );
}


/*
 * Acionamento manual do alarme.
 *
 * Chamado pelo MQTT quando recebe ON.
 */
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

    relay_on();
}


/*
 * Pessoa detectada.
 */
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
     * Liga relé e LED.
     */
    relay_on();


    /*
     * Envia alerta MQTT.
     */
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


    /*
     * Captura as fotos.
     */
    bool captura_ok =
        event_camera_capture_event();

    if (captura_ok)
    {
        ESP_LOGI(
            TAG,
            "Captura das 10 fotos concluida com sucesso"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Falha durante a captura das fotos"
        );
    }
}


/*
 * Processamento dos eventos.
 */
void event_manager_process(void)
{
    /*
     * Futuramente:
     *
     * - fotos a cada 5 segundos
     * - fila de eventos
     * - upload
     * - comandos
     */
}


/*
 * Desarma o alarme.
 */
void event_manager_desarmar(void)
{
    if (!alarme_ativo)
    {
        /*
         * Mesmo que o estado lógico diga OFF,
         * garantimos que o relé físico também
         * fique desligado.
         */
        ESP_LOGW(
            TAG,
            "Desarmar solicitado, mas alarme ja esta desligado"
        );

        relay_off();

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

    /*
     * Primeiro desliga fisicamente.
     */
    relay_off();

    /*
     * Depois atualiza o estado lógico.
     */
    alarme_ativo = false;
}