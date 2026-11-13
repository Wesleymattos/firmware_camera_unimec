
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

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
#include "teste_modem.h"
#include "ota_manager.h"
#include "modem_y7080e.h"

#include "hc_sr501.h"


static const char *TAG = "MAIN";

#define FIRMWARE_VERSION "1.0.2-dev_teste"


void app_main(void)
{
    /*
     * ========================================================
     * IDENTIFICACAO DO FIRMWARE
     * ========================================================
     */

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "UNIMEC CAMERA");
    ESP_LOGI(TAG, "Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "========================================");


    /*
     * ========================================================
     * INICIALIZACAO DA NVS
     * ========================================================
     *
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


    /*
     * ========================================================
     * TESTE MANUAL DO MODEM
     * ========================================================
     */

    /*
    ESP_LOGI(TAG, "Iniciando teste manual do modem...");
    teste_modem();
    ESP_LOGI(TAG, "Teste manual encerrado.");
    */


    /*
     * ========================================================
     * INFRAESTRUTURA DE REDE
     * ========================================================
     *
     * Inicializa a infraestrutura de rede
     * necessária para o modem PPP.
     */

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }

    //system_info_init();


    /*
     * ========================================================
     * WI-FI
     * ========================================================
     *
     * Wi-Fi desativado.
     * A conexão de dados será feita pelo modem.
     */

    wifi_manager_init();


    /*
     * ========================================================
     * CAMERA
     * ========================================================
     */

    /*
     * Inicializa câmera.
     */

    camera_init();


    /*
     * ========================================================
     * CARTAO SD
     * ========================================================
     */

    /*
     * Inicializa cartão SD.
     */

    sd_storage_init();


    /*
     * ========================================================
     * OTA
     * ========================================================
     */

    /*
     * Inicializa módulo OTA.
     */

    //ESP_ERROR_CHECK(ota_manager_init());


    /*
     * ========================================================
     * MODEM CELULAR
     * ========================================================
     */

    /*
     * Inicializa modem celular.
     */

    //modem_init();


    /*
     * ========================================================
     * MODEM Y7080E
     * ========================================================
     */

    /*
     * Inicializa modem Y7080E.
     */

    /*
    ESP_LOGI(TAG, "Inicializando modem Y7080E...");

    esp_err_t modem_err = modem_y7080e_init();

    if (modem_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Y7080E nao iniciou: %s",
                 esp_err_to_name(modem_err));
    }
    */


    /*
     * ========================================================
     * SENSOR VL53L0X
     * ========================================================
     */

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
     * ========================================================
     * SENSOR DE MOVIMENTO HC-SR501
     * ========================================================
     */

    /*
     * Inicializa sensor de movimento HC-SR501.
     */

    hc_sr501_init();


    /*
     * ========================================================
     * LED RGB DE STATUS
     * ========================================================
     *
     * Vermelho = relé desligado
     * Azul     = relé ligado
     */

    led_status_init();


    /*
     * ========================================================
     * RELE
     * ========================================================
     */

    /*
     * Inicializa relé.
     */

   // relay_init();


    /*
     * ========================================================
     * DETECTOR
     * ========================================================
     */

    /*
     * Inicializa detector.
     */

    detector_init();


    /*
     * ========================================================
     * CAPTURA DE FOTOS
     * ========================================================
     */

    /*
     * Inicializa módulo de captura
     * de fotos dos eventos.
     */

    event_camera_init();


    /*
     * ========================================================
     * GERENCIADOR DE EVENTOS
     * ========================================================
     */

    /*
     * Inicializa gerenciador de eventos.
     */

    event_manager_init();


    /*
     * ========================================================
     * SERVICOS DE INTERNET
     * ========================================================
     *
     * Controle de inicialização dos serviços
     * dependentes da conexão de internet.
     */


    /*
     * ========================================================
     * LOOP PRINCIPAL
     * ========================================================
     *
     * O VL53L0X e o detector continuam funcionando
     * mesmo enquanto o modem está conectando.
     */





    while (1)
    {





 
        /*
         * ----------------------------------------------------
         * VERIFICA CONEXAO DO MODEM
         * ----------------------------------------------------
         */

        /*
        if (modem_is_connected() &&
            !servicos_inicializados)
        */

        /*
        static bool estado_anterior = false;

        bool conectado = modem_y7080e_is_connected();

        if (conectado != estado_anterior)
        {
            if (conectado)
            {
                ESP_LOGI(TAG, "Y7080E conectado!");
            }
            else
            {
                ESP_LOGW(TAG, "Y7080E desconectado!");
            }

            estado_anterior = conectado;
        }
        */


        /*
         * ----------------------------------------------------
         * INICIALIZACAO MQTT
         * ----------------------------------------------------
         */

        mqtt_manager_init();


        /*
         * ----------------------------------------------------
         * UPLOAD FTP
         * ----------------------------------------------------
         */

        cloud_upload_init();


        /*
         * ----------------------------------------------------
         * SERVIDOR HTTP DA CAMERA
         * ----------------------------------------------------
         */

        //web_server_init();

        //servicos_inicializados = true;


        /*
         * ----------------------------------------------------
         * LEITURA DO VL53L0X
         * ----------------------------------------------------
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
         * ----------------------------------------------------
         * TESTE DO SENSOR HC-SR501
         * ----------------------------------------------------
         */
/*
        int estado = gpio_get_level(GPIO_NUM_2);

        if (estado == 1)
        {
            ESP_LOGI(TAG, "HC-SR501: MOVIMENTO DETECTADO!");
        }
        else
        {
            ESP_LOGI(TAG, "HC-SR501: SEM MOVIMENTO");
        }

*/




                    /*
            * TESTE DO RELE
            */


            /*
            ESP_LOGI(TAG, "Ligando rele...");
            relay_on();

            vTaskDelay(pdMS_TO_TICKS(3000));

            ESP_LOGI(TAG, "Desligando rele...");
            relay_off();

            vTaskDelay(pdMS_TO_TICKS(3000));

            */

        /*
         * ----------------------------------------------------
         * DETECTOR DE PESSOA
         * ----------------------------------------------------
         */

        
        if (detector_pessoa_detectada())
        {
            event_manager_pessoa_detectada();
        }
        


        /*
         * ----------------------------------------------------
         * PROCESSAMENTO DOS EVENTOS
         * ----------------------------------------------------
         */

        event_manager_process();


        /*
         * ----------------------------------------------------
         * INFORMACOES DO SISTEMA
         * ----------------------------------------------------
         */

        system_info_log();


        /*
         * ----------------------------------------------------
         * PROCESSAMENTO DO MODEM
         * ----------------------------------------------------
         */

        //modem_process();


        /*
         * ----------------------------------------------------
         * INTERVALO DE LEITURA
         * ----------------------------------------------------
         */

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
