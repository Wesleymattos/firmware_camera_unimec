#include "cloud_upload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "FtpClient.h"


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


static NetBuf_t *ftp_netbuf = NULL;


/* ============================================================
 * INICIALIZACAO
 * ============================================================ */

void cloud_upload_init(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Inicializando modulo FTP...");
    ESP_LOGI(TAG, "Servidor: %s:%d", FTP_SERVER, FTP_PORT);
    ESP_LOGI(TAG, "Equipamento: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "======================================");
}


/* ============================================================
 * FUNCAO AUXILIAR
 * ============================================================ */

static bool ftp_entrar_ou_criar_pasta(
    FtpClient *ftp,
    const char *folder
)
{
    int result;


    ESP_LOGI(
        TAG,
        "Verificando pasta: %s",
        folder
    );


    /*
     * Tenta entrar na pasta.
     */
    result = ftp->ftpClientChangeDir(
        folder,
        ftp_netbuf
    );


    /*
     * Se conseguiu, a pasta ja existe.
     */
    if (result == 1)
    {
        ESP_LOGI(
            TAG,
            "Dentro da pasta: %s",
            folder
        );

        return true;
    }


    /*
     * A pasta nao existe.
     * Vamos criar.
     */
    ESP_LOGW(
        TAG,
        "Pasta nao existe. Criando: %s",
        folder
    );


    result = ftp->ftpClientMakeDir(
        folder,
        ftp_netbuf
    );


    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar a pasta: %s",
            folder
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Pasta criada: %s",
        folder
    );


    /*
     * Entra na pasta criada.
     */
    result = ftp->ftpClientChangeDir(
        folder,
        ftp_netbuf
    );


    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel entrar na pasta: %s",
            folder
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Dentro da pasta: %s",
        folder
    );


    return true;
}


/* ============================================================
 * UPLOAD DE UM EVENTO
 * ============================================================ */

bool cloud_upload_event(unsigned int event_id)
{
    char event_folder[32];
    char local_file[128];
    char remote_file[32];


    ESP_LOGI(
        TAG,
        "======================================"
    );

    ESP_LOGI(
        TAG,
        "INICIANDO UPLOAD DO EVENTO"
    );

    ESP_LOGI(
        TAG,
        "Evento: %06u",
        event_id
    );

    ESP_LOGI(
        TAG,
        "Fotos: %d",
        EVENT_PHOTO_COUNT
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );


    /* --------------------------------------------------------
     * 1. Cria cliente FTP
     * -------------------------------------------------------- */

    FtpClient *ftp = getFtpClient();


    if (ftp == NULL)
    {
        ESP_LOGE(
            TAG,
            "Falha ao criar cliente FTP!"
        );

        return false;
    }


    /* --------------------------------------------------------
     * 2. Conecta
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Conectando ao servidor FTP..."
    );


    int result = ftp->ftpClientConnect(
        FTP_SERVER,
        FTP_PORT,
        &ftp_netbuf
    );


    if (result == 0)
    {
        ESP_LOGE(
            TAG,
            "Falha ao conectar no FTP!"
        );

        return false;
    }


    ESP_LOGI(
        TAG,
        "Conexao FTP estabelecida!"
    );


    /* --------------------------------------------------------
     * 3. Login
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "Autenticando..."
    );


    result = ftp->ftpClientLogin(
        FTP_USER,
        FTP_PASSWORD,
        ftp_netbuf
    );


    if (result == 0)
    {
        ESP_LOGE(
            TAG,
            "Falha na autenticacao FTP!"
        );

        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    ESP_LOGI(
        TAG,
        "Login FTP realizado!"
    );


    /* --------------------------------------------------------
     * 4. Pasta raiz
     * -------------------------------------------------------- */

    if (!ftp_entrar_ou_criar_pasta(
            ftp,
            CAMERA_ROOT_FOLDER))
    {
        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    /* --------------------------------------------------------
     * 5. Pasta da camera
     * -------------------------------------------------------- */

    if (!ftp_entrar_ou_criar_pasta(
            ftp,
            DEVICE_NAME))
    {
        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    /* --------------------------------------------------------
     * 6. Nome da pasta do evento
     * -------------------------------------------------------- */

    snprintf(
        event_folder,
        sizeof(event_folder),
        "event_%06u",
        event_id
    );


    /* --------------------------------------------------------
     * 7. Pasta do evento
     * -------------------------------------------------------- */

    if (!ftp_entrar_ou_criar_pasta(
            ftp,
            event_folder))
    {
        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    /* --------------------------------------------------------
     * 8. Envia as 10 fotos
     * -------------------------------------------------------- */

    for (
        unsigned int i = 1;
        i <= EVENT_PHOTO_COUNT;
        i++
    )
    {
        /*
         * Caminho local:
         *
         * /sdcard/events/event_000003/photo_01.jpg
         */
        snprintf(
            local_file,
            sizeof(local_file),
            "/sdcard/events/%s/photo_%02u.jpg",
            event_folder,
            i
        );


        /*
         * Nome remoto:
         *
         * photo_01.jpg
         */
        snprintf(
            remote_file,
            sizeof(remote_file),
            "photo_%02u.jpg",
            i
        );


        ESP_LOGI(
            TAG,
            "--------------------------------------"
        );

        ESP_LOGI(
            TAG,
            "Enviando foto %u/%d",
            i,
            EVENT_PHOTO_COUNT
        );

        ESP_LOGI(
            TAG,
            "Local : %s",
            local_file
        );

        ESP_LOGI(
            TAG,
            "Remoto: %s",
            remote_file
        );


        /* ----------------------------------------------------
         * Verifica se o arquivo existe no SD
         * ---------------------------------------------------- */

        FILE *file = fopen(
            local_file,
            "rb"
        );


        if (file == NULL)
        {
            ESP_LOGE(
                TAG,
                "Arquivo nao encontrado: %s",
                local_file
            );

            ftp->ftpClientQuit(ftp_netbuf);

            ftp_netbuf = NULL;

            return false;
        }


        fclose(file);


        /* ----------------------------------------------------
         * Upload
         * ---------------------------------------------------- */

        result = ftp->ftpClientPut(
            local_file,
            remote_file,
            FTP_CLIENT_BINARY,
            ftp_netbuf
        );


        if (result <= 0)
        {
            ESP_LOGE(
                TAG,
                "Falha ao enviar foto %u/%d!",
                i,
                EVENT_PHOTO_COUNT
            );

            ftp->ftpClientQuit(ftp_netbuf);

            ftp_netbuf = NULL;

            return false;
        }


        ESP_LOGI(
            TAG,
            "Foto %u/%d enviada com sucesso!",
            i,
            EVENT_PHOTO_COUNT
        );
    }


    /* --------------------------------------------------------
     * 9. Final
     * -------------------------------------------------------- */

    ESP_LOGI(
        TAG,
        "======================================"
    );

    ESP_LOGI(
        TAG,
        "UPLOAD DO EVENTO CONCLUIDO!"
    );

    ESP_LOGI(
        TAG,
        "Equipamento: %s",
        DEVICE_NAME
    );

    ESP_LOGI(
        TAG,
        "Evento: %s",
        event_folder
    );

    ESP_LOGI(
        TAG,
        "Fotos enviadas: %d",
        EVENT_PHOTO_COUNT
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );


    /* --------------------------------------------------------
     * 10. Fecha FTP
     * -------------------------------------------------------- */

    ftp->ftpClientQuit(
        ftp_netbuf
    );

    ftp_netbuf = NULL;


    return true;
}


/* ============================================================
 * TASK DE UPLOAD DE EVENTO
 * ============================================================ */

static void cloud_upload_event_task(void *arg)
{
    /*
     * O event_id foi alocado dinamicamente
     * antes da criação desta task.
     */
    unsigned int event_id =
        *((unsigned int *)arg);


    /*
     * Agora que copiamos o valor para uma
     * variável local, podemos liberar a memória.
     */
    free(arg);


    ESP_LOGI(
        TAG,
        "======================================"
    );

    ESP_LOGI(
        TAG,
        "TASK DE UPLOAD INICIADA"
    );

    ESP_LOGI(
        TAG,
        "Evento: %06u",
        event_id
    );

    ESP_LOGI(
        TAG,
        "Stack: %d bytes",
        FTP_TASK_STACK_SIZE
    );

    ESP_LOGI(
        TAG,
        "======================================"
    );


    /*
     * Executa o upload completo.
     *
     * Esta funcao agora roda dentro da
     * task FTP, e nao mais dentro da main_task.
     */
    bool result =
        cloud_upload_event(event_id);


    if (result)
    {
        ESP_LOGI(
            TAG,
            "======================================"
        );

        ESP_LOGI(
            TAG,
            "UPLOAD DO EVENTO %06u CONCLUIDO!",
            event_id
        );

        ESP_LOGI(
            TAG,
            "======================================"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "======================================"
        );

        ESP_LOGE(
            TAG,
            "UPLOAD DO EVENTO %06u FALHOU!",
            event_id
        );

        ESP_LOGE(
            TAG,
            "======================================"
        );
    }


    ESP_LOGI(
        TAG,
        "Task FTP finalizada"
    );


    /*
     * Finaliza a task.
     */
    vTaskDelete(NULL);
}


/* ============================================================
 * INICIA UPLOAD DE EVENTO
 * ============================================================ */

void cloud_upload_start_event(
    unsigned int event_id
)
{
    /*
     * Aloca memoria para passar o event_id
     * para a nova task.
     *
     * Nao podemos passar o endereco de uma
     * variavel local da funcao, pois ela deixaria
     * de existir depois que esta funcao terminasse.
     */
    unsigned int *event_id_ptr =
        malloc(sizeof(unsigned int));


    if (event_id_ptr == NULL)
    {
        ESP_LOGE(
            TAG,
            "Falha ao alocar memoria para event_id!"
        );

        return;
    }


    *event_id_ptr = event_id;


    ESP_LOGI(
        TAG,
        "Criando task FTP para o evento %06u...",
        event_id
    );


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
        ESP_LOGE(
            TAG,
            "Falha ao criar task FTP para evento %06u!",
            event_id
        );

        /*
         * Se a task nao foi criada,
         * precisamos liberar a memoria.
         */
        free(event_id_ptr);

        return;
    }


    ESP_LOGI(
        TAG,
        "Task FTP criada para evento %06u!",
        event_id
    );
}


/* ============================================================
 * TESTE FTP ANTIGO
 * ============================================================ */

bool cloud_upload_test(void)
{
    /*
     * Mantemos a funcao antiga temporariamente.
     *
     * O novo sistema usa cloud_upload_event().
     */

    return cloud_upload_event(1);
}


/* ============================================================
 * TASK FTP DE TESTE
 * ============================================================ */

static void cloud_upload_test_task(void *arg)
{
    ESP_LOGI(
        TAG,
        "Task FTP de teste iniciada"
    );


    bool result =
        cloud_upload_test();


    if (result)
    {
        ESP_LOGI(
            TAG,
            "Upload FTP realizado com sucesso!"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Upload FTP falhou!"
        );
    }


    ESP_LOGI(
        TAG,
        "Task FTP de teste finalizada"
    );


    vTaskDelete(NULL);
}


/* ============================================================
 * INICIA TESTE
 * ============================================================ */

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
        ESP_LOGE(
            TAG,
            "Falha ao criar task FTP!"
        );
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Task FTP criada com sucesso!"
        );
    }
}