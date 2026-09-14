#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "system_info.h"
#include "wifi_manager.h"
#include "camera.h"
#include "web_server.h"
#include "relay.h"
#include "led_status.h"

#include "detector.h"
#include "event_camera.h"
#include "event_manager.h"
#include "sd_storage.h"
#include "mqtt_manager.h"
#include "cloud_upload.h"

void app_main(void)
{
    system_info_init();

    /*
     * Inicializa Wi-Fi.
     *
     * A conexão acontece de forma assíncrona.
     */
    wifi_manager_init();

    /*
     * Inicializa câmera.
     */
    camera_init();

    /*
     * Inicializa cartão SD.
     */
    sd_storage_init();

    /*
     * Inicializa LED RGB de status.
     *
     * Vermelho = relé desligado
     * Azul     = relé ligado
     */
    led_status_init();

    /*
     * Inicializa relé.
     */
    relay_init();

    /*
     * Inicializa detector.
     *
     * Por enquanto está em modo de teste.
     */
    detector_init();

    /*
     * Inicializa módulo de captura
     * de fotos dos eventos.
     */
    event_camera_init();

    /*
     * Inicializa gerenciador de eventos.
     */
    event_manager_init();

    /*
     * Aguarda o Wi-Fi receber IP
     * antes de iniciar serviços
     * que dependem da Internet.
     */
    ESP_LOGI(
        "MAIN",
        "Aguardando conexao WiFi..."
    );

    while (!wifi_manager_is_connected())
    {
        vTaskDelay(
            pdMS_TO_TICKS(100)
        );
    }

    ESP_LOGI(
        "MAIN",
        "WiFi conectado. Inicializando MQTT..."
    );

   mqtt_manager_init();

/*
 * Inicializa módulo de upload FTP.
 */
cloud_upload_init();

/*
 * Inicia o teste FTP em uma task própria.
 */
ESP_LOGI(
    "MAIN",
    "Iniciando teste de upload FTP..."
);

cloud_upload_start_test();

    /*
     * Inicializa servidor HTTP
     * da câmera.
     */
    web_server_init();

    /*
     * Loop principal.
     */
    while (1)
    {
        /*
         * Verifica se houve detecção.
         *
         * Por enquanto o detector
         * é FICTÍCIO.
         */
        if (detector_pessoa_detectada())
        {
            event_manager_pessoa_detectada();
        }

        /*
         * Processa eventos pendentes.
         *
         * Futuramente:
         *
         * - fotos a cada 5 segundos
         * - fila de eventos
         * - upload FTP
         * - reenvio de arquivos
         * - comunicação com servidor
         */
        event_manager_process();

        /*
         * Informações periódicas
         * do sistema.
         */
        system_info_log();

        /*
         * Aguarda 1 segundo.
         */
        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}