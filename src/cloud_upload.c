
#include "cloud_upload.h"
#include "ftp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "modem_y7080e.h"

static const char *TAG = "CLOUD_UPLOAD";

/* ============================================================
 * CONFIGURACAO FTP
 * ============================================================ */

#define FTP_SERVER          "200.234.212.167"
#define FTP_PORT            21

#define FTP_USER            "nemosftp"
#define FTP_PASSWORD        "UniMec@FTP2026!"

#define DEVICE_NAME         "unimec-camera-01"
#define CAMERA_ROOT_FOLDER  "unimec-cameras"

#define EVENT_PHOTO_COUNT   10

/* ============================================================
 * TASK
 * ============================================================ */

#define FTP_TASK_STACK_SIZE     (16 * 1024)
#define FTP_TASK_PRIORITY       5

/* Impede duas transferencias simultaneas pelo mesmo modem. */
static SemaphoreHandle_t s_upload_mutex = NULL;

/* ============================================================
 * INICIALIZACAO
 * ============================================================ */

void cloud_upload_init(void)
{
    if (s_upload_mutex == NULL)
    {
        s_upload_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Inicializando modulo FTP Y7080E...");
    ESP_LOGI(TAG, "Servidor: %s:%d", FTP_SERVER, FTP_PORT);
    ESP_LOGI(TAG, "Equipamento: %s", DEVICE_NAME);

    if (s_upload_mutex == NULL)
    {
        ESP_LOGE(TAG, "Falha ao criar mutex FTP!");
    }

    ESP_LOGI(TAG, "======================================");
}

/* ============================================================
 * NAVEGACAO DE DIRETORIOS
 * ============================================================ */

static bool ftp_entrar_ou_criar_pasta(
    ftp_client_t *ftp,
    const char *folder)
{
    ESP_LOGI(TAG, "Verificando diretorio: %s", folder);

    esp_err_t err = ftp_client_change_dir(ftp, folder);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Diretorio acessado: %s", folder);
        return true;
    }

    ESP_LOGW(TAG, "CWD falhou. Tentando criar: %s", folder);

    err = ftp_client_make_dir(ftp, folder);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao criar diretorio: %s (err=%s)",
                 folder, esp_err_to_name(err));
        return false;
    }

    err = ftp_client_change_dir(ftp, folder);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao acessar diretorio criado: %s",
                 folder);
        return false;
    }

    ESP_LOGI(TAG, "Diretorio criado e acessado: %s", folder);
    return true;
}

/* ============================================================
 * UPLOAD DE UM ARQUIVO
 * ============================================================ */

static bool ftp_upload_file(
    ftp_client_t *ftp,
    const char *local_path,
    const char *remote_path)
{
    struct stat st;

    if (stat(local_path, &st) != 0)
    {
        ESP_LOGE(TAG, "Arquivo local nao encontrado: %s",
                 local_path);
        return false;
    }

    ESP_LOGI(TAG, "Arquivo: %s | Tamanho: %ld bytes",
             local_path, (long)st.st_size);

    /*
     * Cada arquivo precisa de uma nova conexao PASV,
     * pois o canal de dados e encerrado apos cada STOR.
     */
    esp_err_t err = ftp_client_open_passive(ftp);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao abrir canal PASV: %s",
                 esp_err_to_name(err));
        return false;
    }

    int64_t start = esp_timer_get_time();

    ESP_LOGI(TAG, "Iniciando STOR: %s", remote_path);

    err = ftp_client_store_file(
        ftp,
        local_path,
        remote_path
    );

    int64_t elapsed_ms =
        (esp_timer_get_time() - start) / 1000;

    /*
     * Protecao adicional: se store_file falhar antes de
     * fechar o socket, tentamos fecha-lo aqui.
     */
    if (err != ESP_OK && ftp->data_socket >= 0)
    {
        ESP_LOGW(TAG, "Fechando socket de dados apos falha");

        modem_y7080e_socket_close(ftp->data_socket);
        ftp->data_socket = -1;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG,
                 "Falha no STOR %s | erro=%s | tempo=%lld ms",
                 remote_path,
                 esp_err_to_name(err),
                 (long long)elapsed_ms);

        return false;
    }

    ESP_LOGI(TAG,
             "Upload concluido: %s | %ld bytes | %lld ms",
             remote_path,
             (long)st.st_size,
             (long long)elapsed_ms);

    return true;
}

/* ============================================================
 * ENCERRAMENTO SEGURO
 * ============================================================ */

static void ftp_encerrar(ftp_client_t *ftp)
{
    if (ftp == NULL)
        return;

    esp_err_t err = ftp_client_quit(ftp);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Erro ao encerrar cliente FTP: %s",
                 esp_err_to_name(err));
    }
}

/* ============================================================
 * UPLOAD COMPLETO DE UM EVENTO
 * ============================================================ */

bool cloud_upload_event(unsigned int event_id)
{
    if (s_upload_mutex == NULL)
    {
        ESP_LOGE(TAG, "FTP nao inicializado. Chame cloud_upload_init()");
        return false;
    }

    /*
     * Apenas uma task pode utilizar o FTP/modem por vez.
     */
    if (xSemaphoreTake(s_upload_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Nao foi possivel obter mutex FTP");
        return false;
    }

    bool success = false;
    ftp_client_t ftp;

    char event_folder[32];
    char local_file[128];
    char remote_file[32];

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "INICIANDO UPLOAD DO EVENTO %06u", event_id);
    ESP_LOGI(TAG, "Fotos: %d", EVENT_PHOTO_COUNT);
    ESP_LOGI(TAG, "======================================");

    /* Inicializa cliente FTP novo. */
    esp_err_t err = ftp_client_init(&ftp);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao inicializar FTP: %s",
                 esp_err_to_name(err));
        goto cleanup;
    }

    /* Conexao de controle. */
    ESP_LOGI(TAG, "Conectando ao servidor FTP...");

    err = ftp_client_connect(&ftp, FTP_SERVER, FTP_PORT);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha na conexao FTP: %s",
                 esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Conexao FTP estabelecida");

    /* Autenticacao. */
    ESP_LOGI(TAG, "Autenticando no FTP...");

    err = ftp_client_login(&ftp, FTP_USER, FTP_PASSWORD);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha no login FTP: %s",
                 esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "Login FTP realizado");

    /* Diretorio raiz. */
    if (!ftp_entrar_ou_criar_pasta(&ftp, CAMERA_ROOT_FOLDER))
        goto cleanup;

    /* Diretorio do equipamento. */
    if (!ftp_entrar_ou_criar_pasta(&ftp, DEVICE_NAME))
        goto cleanup;

    /* Nome do evento. */
    snprintf(
        event_folder,
        sizeof(event_folder),
        "event_%06u",
        event_id
    );

    /* Diretorio do evento. */
    if (!ftp_entrar_ou_criar_pasta(&ftp, event_folder))
        goto cleanup;

    /* ========================================================
     * ENVIO DAS 10 FOTOS
     * ======================================================== */

    for (unsigned int i = 1; i <= EVENT_PHOTO_COUNT; i++)
    {
        snprintf(
            local_file,
            sizeof(local_file),
            "/sdcard/events/%s/photo_%02u.jpg",
            event_folder,
            i
        );

        snprintf(
            remote_file,
            sizeof(remote_file),
            "photo_%02u.jpg",
            i
        );

        ESP_LOGI(TAG, "--------------------------------------");
        ESP_LOGI(TAG, "Enviando foto %u/%d", i, EVENT_PHOTO_COUNT);

        if (!ftp_upload_file(&ftp, local_file, remote_file))
        {
            ESP_LOGE(TAG, "Falha ao enviar foto %u", i);
            goto cleanup;
        }

        ESP_LOGI(TAG, "Foto %u/%d enviada", i, EVENT_PHOTO_COUNT);
    }

    /* ========================================================
     * ENVIO DO METADATA
     * ======================================================== */

    snprintf(
        local_file,
        sizeof(local_file),
        "/sdcard/events/%s/metadata.txt",
        event_folder
    );

    snprintf(
        remote_file,
        sizeof(remote_file),
        "metadata.txt"
    );

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Enviando metadata.txt");

    if (!ftp_upload_file(&ftp, local_file, remote_file))
    {
        ESP_LOGE(TAG, "Falha ao enviar metadata");
        goto cleanup;
    }

    /* Sucesso. */
    success = true;

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "UPLOAD DO EVENTO CONCLUIDO!");
    ESP_LOGI(TAG, "Equipamento: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Evento: %s", event_folder);
    ESP_LOGI(TAG, "Fotos enviadas: %d", EVENT_PHOTO_COUNT);
    ESP_LOGI(TAG, "Metadata enviada: SIM");
    ESP_LOGI(TAG, "======================================");

cleanup:

    /*
     * Encerra os sockets mesmo quando alguma etapa falha.
     */
    ftp_encerrar(&ftp);

    xSemaphoreGive(s_upload_mutex);

    return success;
}

/* ============================================================
 * TASK DE UPLOAD
 * ============================================================ */

static void cloud_upload_event_task(void *arg)
{
    unsigned int event_id = *((unsigned int *)arg);

    free(arg);

    ESP_LOGI(TAG, "Task FTP iniciada para evento %06u", event_id);

    bool result = cloud_upload_event(event_id);

    if (result)
    {
        ESP_LOGI(TAG, "Evento %06u enviado com sucesso!", event_id);
    }
    else
    {
        ESP_LOGE(TAG, "Falha no upload do evento %06u", event_id);
    }

    ESP_LOGI(TAG, "Task FTP finalizada");

    vTaskDelete(NULL);
}

/* ============================================================
 * INICIA UPLOAD DE EVENTO
 * ============================================================ */

void cloud_upload_start_event(unsigned int event_id)
{
    unsigned int *event_id_ptr = malloc(sizeof(unsigned int));

    if (event_id_ptr == NULL)
    {
        ESP_LOGE(TAG, "Falha ao alocar memoria para event_id");
        return;
    }

    *event_id_ptr = event_id;

    BaseType_t result = xTaskCreate(
        cloud_upload_event_task,
        "ftp_event",
        FTP_TASK_STACK_SIZE,
        event_id_ptr,
        FTP_TASK_PRIORITY,
        NULL
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar task FTP");
        free(event_id_ptr);
        return;
    }

    ESP_LOGI(TAG, "Task FTP criada para evento %06u", event_id);
}

/* ============================================================
 * TESTE FTP
 * ============================================================ */

bool cloud_upload_test(void)
{
    return cloud_upload_event(1);
}

static void cloud_upload_test_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "Task FTP de teste iniciada");

    bool result = cloud_upload_test();

    if (result)
        ESP_LOGI(TAG, "Teste FTP concluido com sucesso");
    else
        ESP_LOGE(TAG, "Teste FTP falhou");

    vTaskDelete(NULL);
}

void cloud_upload_start_test(void)
{
    BaseType_t result = xTaskCreate(
        cloud_upload_test_task,
        "ftp_test",
        FTP_TASK_STACK_SIZE,
        NULL,
        FTP_TASK_PRIORITY,
        NULL
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Falha ao criar task FTP de teste");
    }
    else
    {
        ESP_LOGI(TAG, "Task FTP de teste criada");
    }
}
