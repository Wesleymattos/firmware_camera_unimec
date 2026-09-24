#include "mqtt_manager.h"

#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

#include "event_manager.h"
#include "detector.h"

static const char *TAG = "MQTT";

/*
 * Configuracao do broker
 */
#define MQTT_BROKER_URI    "mqtt://200.234.212.167:1883"
#define MQTT_USERNAME      "unimec"
#define MQTT_PASSWORD      "UniMec@MQTT2026!"
#define MQTT_CLIENT_ID     "unimec-camera-01"

/*
 * Topico para enviar alertas.
 */
#define MQTT_TOPIC_ALERT   "unimec/test"

/*
 * Topico para receber comandos.
 */
#define MQTT_TOPIC_COMMAND "unimec/camera-01/command"

/*
 * Comandos aceitos.
 */
#define MQTT_COMMAND_ON    "ON"
#define MQTT_COMMAND_OFF   "OFF"

static esp_mqtt_client_handle_t mqtt_client = NULL;

static bool mqtt_connected = false;


/*
 * Handler de eventos MQTT.
 */
static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        /*
         * MQTT conectado.
         */
        case MQTT_EVENT_CONNECTED:
        {
            mqtt_connected = true;

            ESP_LOGI(
                TAG,
                "======================================"
            );

            ESP_LOGI(
                TAG,
                "MQTT conectado!"
            );

            ESP_LOGI(
                TAG,
                "Broker: %s",
                MQTT_BROKER_URI
            );

            ESP_LOGI(
                TAG,
                "======================================"
            );

            /*
             * Inscreve no topico de comandos.
             */
            int msg_id = esp_mqtt_client_subscribe(
                mqtt_client,
                MQTT_TOPIC_COMMAND,
                0
            );

            if (msg_id >= 0)
            {
                ESP_LOGI(
                    TAG,
                    "Inscrito no topico de comandos:"
                );

                ESP_LOGI(
                    TAG,
                    "%s",
                    MQTT_TOPIC_COMMAND
                );
            }
            else
            {
                ESP_LOGE(
                    TAG,
                    "Falha ao se inscrever no topico de comandos!"
                );
            }

            break;
        }


        /*
         * MQTT desconectado.
         */
        case MQTT_EVENT_DISCONNECTED:

            mqtt_connected = false;

            ESP_LOGW(
                TAG,
                "MQTT desconectado"
            );

            break;


        /*
         * Mensagem MQTT recebida.
         */
        case MQTT_EVENT_DATA:
        {
            ESP_LOGI(
                TAG,
                "======================================"
            );

            ESP_LOGI(
                TAG,
                "Mensagem MQTT recebida!"
            );

            ESP_LOGI(
                TAG,
                "Topic: %.*s",
                event->topic_len,
                event->topic
            );

            ESP_LOGI(
                TAG,
                "Payload: %.*s",
                event->data_len,
                event->data
            );

            ESP_LOGI(
                TAG,
                "======================================"
            );


            /*
             * Verifica se a mensagem veio do
             * topico de comandos.
             */
            if (
                event->topic_len ==
                    strlen(MQTT_TOPIC_COMMAND)
                &&
                strncmp(
                    event->topic,
                    MQTT_TOPIC_COMMAND,
                    event->topic_len
                ) == 0
            )
            {
                /*
                 * ==============================
                 * COMANDO ON
                 * ==============================
                 */
                if (
                    event->data_len ==
                        strlen(MQTT_COMMAND_ON)
                    &&
                    strncmp(
                        event->data,
                        MQTT_COMMAND_ON,
                        event->data_len
                    ) == 0
                )
                {
                    ESP_LOGW(
                        TAG,
                        "Comando ON recebido!"
                    );

                    /*
                     * Liga o alarme.
                     *
                     * O relay.c tambem atualiza
                     * automaticamente o LED RGB.
                     */
                    event_manager_acionar();
                }


                /*
                 * ==============================
                 * COMANDO OFF
                 * ==============================
                 */
                else if (
                    event->data_len ==
                        strlen(MQTT_COMMAND_OFF)
                    &&
                    strncmp(
                        event->data,
                        MQTT_COMMAND_OFF,
                        event->data_len
                    ) == 0
                )
                {
                    ESP_LOGW(
                        TAG,
                        "Comando OFF recebido!"
                    );

                    /*
                     * Desarma o sistema.
                     */
                    event_manager_desarmar();

                    /*
                     * Reseta o detector para que
                     * fique novamente preparado
                     * para uma nova deteccao.
                     */
                    detector_reset();
                }


                /*
                 * ==============================
                 * COMANDO DESCONHECIDO
                 * ==============================
                 */
                else
                {
                    ESP_LOGW(
                        TAG,
                        "Comando desconhecido recebido!"
                    );
                }
            }

            break;
        }


        /*
         * Erro MQTT.
         */
        case MQTT_EVENT_ERROR:

            ESP_LOGE(
                TAG,
                "Erro no MQTT"
            );

            break;


        default:
            break;
    }
}


/*
 * Inicializa MQTT.
 */
void mqtt_manager_init(void)
{
    ESP_LOGI(
        TAG,
        "Inicializando MQTT..."
    );


    esp_mqtt_client_config_t mqtt_config = {

        .broker.address.uri =
            MQTT_BROKER_URI,

        .credentials.username =
            MQTT_USERNAME,

        .credentials.authentication.password =
            MQTT_PASSWORD,

        .credentials.client_id =
            MQTT_CLIENT_ID
    };


    mqtt_client =
        esp_mqtt_client_init(
            &mqtt_config
        );


    if (mqtt_client == NULL)
    {
        ESP_LOGE(
            TAG,
            "Falha ao criar cliente MQTT!"
        );

        return;
    }


    esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );


    esp_err_t ret =
        esp_mqtt_client_start(
            mqtt_client
        );


    if (ret != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Falha ao iniciar MQTT: %s",
            esp_err_to_name(ret)
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Cliente MQTT iniciado"
    );
}


/*
 * Retorna estado da conexao MQTT.
 */
bool mqtt_manager_is_connected(void)
{
    return mqtt_connected;
}


/*
 * Publica alerta de pessoa detectada.
 */
bool mqtt_manager_publish_alert(void)
{
    if (!mqtt_connected)
    {
        ESP_LOGW(
            TAG,
            "MQTT nao conectado. Alerta nao enviado."
        );

        return false;
    }


    const char *message =
        "Alerta, pessoa identificada, UNIMEC Security Camera";


    int msg_id =
        esp_mqtt_client_publish(
            mqtt_client,
            MQTT_TOPIC_ALERT,
            message,
            0,
            0,
            0
        );


    if (msg_id < 0)
    {
        ESP_LOGE(
            TAG,
            "Falha ao publicar alerta!"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Alerta MQTT enviado!"
    );

    ESP_LOGI(
        TAG,
        "Topic: %s",
        MQTT_TOPIC_ALERT
    );

    ESP_LOGI(
        TAG,
        "Mensagem: %s",
        message
    );


    return true;
}