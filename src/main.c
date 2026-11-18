
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

#include "modem.h"
#include "teste_modem.h"
#include "ota_manager.h"
#include "modem_y7080e.h"

#include "hc_sr501.h"


static const char *TAG = "MAIN";

#define FIRMWARE_VERSION "1.0.2-dev_teste"

#define MODEM_INIT_TASK_STACK 12288
#define MODEM_INIT_TASK_PRIORITY 5


/* =========================================================
   TASK DE INICIALIZACAO DO MODEM Y7080E
   ========================================================= */

static void modem_init_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task de inicializacao do Y7080E iniciada");

esp_err_t err = modem_y7080e_init();

if (err == ESP_OK)
{
    ESP_LOGI(TAG, "Y7080E inicializado com sucesso!");
}
else
{
    ESP_LOGE(TAG,
             "Falha na inicializacao do Y7080E: %s",
             esp_err_to_name(err));
}

    ESP_LOGI(TAG, "Stack livre restante: %u bytes",
             (unsigned int)uxTaskGetStackHighWaterMark(NULL) *
             sizeof(StackType_t));

    vTaskDelete(NULL);
}


/* =========================================================
   APP MAIN
   ========================================================= */

void app_main(void)
{
    
    esp_err_t err;

    /*
     * ========================================================
     * 1. IDENTIFICACAO DO FIRMWARE
     * ========================================================
     */

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "UNIMEC CAMERA");
    ESP_LOGI(TAG, "Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "========================================");


    /*
     * ========================================================
     * 2. NVS
     * ========================================================
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
     * 3. INFRAESTRUTURA DE REDE
     * ========================================================
     */

    //ESP_ERROR_CHECK(esp_netif_init());

    err = esp_event_loop_create_default();

    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(err);
    }


    /*
     * ========================================================
     * 4. RELE E LED
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando rele...");
    //relay_init();

    //ESP_LOGI(TAG, "Inicializando LED...");
    //led_status_init();


    /*
     * ========================================================
     * 5. ARMAZENAMENTO
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando cartao SD...");
    //sd_storage_init();


    /*
     * ========================================================
     * 6. CAMERA
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando camera OV3660...");
    //camera_init();


    /*
     * ========================================================
     * 7. SENSOR DE MOVIMENTO
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando HC-SR501...");
    //hc_sr501_init();


    /*
     * ========================================================
     * 8. DETECTOR
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando detector...");
    //detector_init();


    /*
     * ========================================================
     * 9. CAPTURA DE FOTOS
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando Event Camera...");
    //event_camera_init();


    /*
     * ========================================================
     * 10. GERENCIADOR DE EVENTOS
     * ========================================================
     */

    //ESP_LOGI(TAG, "Inicializando Event Manager...");
    //event_manager_init();

    //ESP_LOGI(TAG, "Inicializando gerenciador FTP...");
    //cloud_upload_init();


    /*
     * ========================================================
     * 11. SERVICOS DE REDE OPCIONAIS
     * ========================================================
     */

    // system_info_init();
    // wifi_manager_init();
    // ESP_ERROR_CHECK(ota_manager_init());
    // modem_init();


    /*
     * ========================================================
     * 12. MODEM Y7080E
     * ========================================================
     *
     * Executa em tarefa independente.
     * A aquisicao de imagens nao aguarda o registro celular.
     */

    ESP_LOGI(TAG, "Inicializando modem Y7080E...");

    BaseType_t task_result = xTaskCreate(
        modem_init_task,
        "modem_init",
        MODEM_INIT_TASK_STACK,
        NULL,
        MODEM_INIT_TASK_PRIORITY,
        NULL
    );

    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar task do modem");
    }
    else
    {
        ESP_LOGI(TAG, "Task do modem criada com sucesso");
    }


    
   

    /*
     * ========================================================
     * 14. LOOP PRINCIPAL
     * ========================================================
     */

       static bool mqtt_iniciado = false;

    while (1)
    {
        /*
         * Verifica estado do modem
         */

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

/*
 * Inicializacao do MQTT apos conexao do modem
 */

if (conectado && !mqtt_iniciado)
{
    ESP_LOGI(TAG, "Modem conectado! Enviando comando ON...");

    esp_err_t mqtt_test = modem_y7080e_mqtt_publish(
        "unimec/camera-01/command",
        "ON"
    );

    if (mqtt_test == ESP_OK)
    {
        ESP_LOGI(TAG, "Comando ON publicado com sucesso!");
    }
    else
    {
        ESP_LOGE(TAG, "Falha ao publicar comando ON!");
    }

    mqtt_iniciado = true;
}


        /*
         * Detector de pessoa / movimento
         */
/*
        if (detector_pessoa_detectada())
        {
            event_manager_pessoa_detectada();
        }

*/
        /*
         * Processamento dos eventos
         */

       // event_manager_process();


        /*
         * Informacoes do sistema
         */

        // system_info_log();


        /*
         * Modem legado
         */

        // modem_process();


        vTaskDelay(pdMS_TO_TICKS(200));
    }

}
