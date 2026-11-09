#include <stdio.h>
#include <stdbool.h>

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

#include "vl53l0x.h"
#include "modem.h"


#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "teste_modem.h"

#include "ota_manager.h"


static const char *TAG = "MAIN";


#define FIRMWARE_VERSION "1.0.1-dev"

void app_main(void)
{

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "UNIMEC CAMERA");
    ESP_LOGI(TAG, "Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "========================================");

  /*
     * Inicializa NVS.
     * Necessário para módulos que armazenam dados persistentes.
     */
    esp_err_t nvs_err = nvs_flash_init();

    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS precisa ser apagada e reinicializada");

        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(nvs_err);

     //teste_modem(); // Comente quando terminar o teste


      /*
     * Inicializa a infraestrutura de rede
     * necessária para o modem PPP.
     */
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }
    system_info_init();

    /*
     * Wi-Fi desativado.
     * A conexão de dados será feita pelo modem.
     */

    
     //wifi_manager_init();
     

    /*
     * Inicializa câmera.
     */
    camera_init();

    /*
     * Inicializa cartão SD.
     */
    sd_storage_init();

    /*
     * Inicializa módulo OTA.
     */
    ESP_ERROR_CHECK(ota_manager_init());

    /*
     * Inicializa modem celular.
     */
    modem_init();

/*
 * Inicializa VL53L0X.
 */
bool vl53l0x_ok = vl53l0x_init();

if (vl53l0x_ok)
{
    ESP_LOGI(TAG, "VL53L0X inicializado com sucesso!");
}
else
{
    ESP_LOGE(TAG, "Falha ao inicializar VL53L0X!");
}

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
     * Controle de inicialização dos serviços
     * dependentes da conexão de internet.
     */
    bool servicos_inicializados = false;

    /*
     * Loop principal.
     *
     * O VL53L0X e o detector continuam funcionando
     * mesmo enquanto o modem está conectando.
     */
    while (1)
    {
        /*
         * Verifica conexão do modem.
         */
        if (modem_is_connected() &&
            !servicos_inicializados)
        {
            ESP_LOGI(
                TAG,
                "Modem conectado. Inicializando servicos..."
            );

            /*
             * Inicializa MQTT.
             */
            mqtt_manager_init();

            /*
             * Inicializa módulo de upload FTP.
             */
            //cloud_upload_init();

            /*
             * Inicializa servidor HTTP da câmera.
             */
            //web_server_init();

            servicos_inicializados = true;

            ESP_LOGI(
                TAG,
                "Servicos inicializados!"
            );
        }

        /*
         * Leitura do VL53L0X.
         */
        uint16_t distancia_mm;

        if (vl53l0x_read_distance(&distancia_mm))
        {
            ESP_LOGI(
                TAG,
                "Distancia VL53L0X: %.2f m",
                distancia_mm / 1000.0f
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Falha na leitura do VL53L0X"
            );
        }

        /*
         * Detector de pessoa.
         */
        if (detector_pessoa_detectada())
        {
            event_manager_pessoa_detectada();
        }



        
        /*
         * Processamento dos eventos.
         */
        event_manager_process();

        /*
         * Informações do sistema.
         */
        system_info_log();

        /*
         * Processamento do modem.
         */
        modem_process();

        vTaskDelay(
            pdMS_TO_TICKS(300)
        );
    }
}