#include "cloud_upload.h"

#include <stdio.h>
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

/* Nome da camera no servidor */
#define DEVICE_NAME         "unimec-camera-01"

/* Evento que vamos testar */
#define EVENT_ID            1

/* Arquivo local */
#define TEST_FILE           "/sdcard/events/event_000001/photo_01.jpg"

/* Nome remoto */
#define REMOTE_FILE         "photo_01.jpg"

#define CAMERA_ROOT_FOLDER  "unimec-cameras"

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
 * TESTE DE UPLOAD
 * ============================================================ */

bool cloud_upload_test(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "Iniciando teste de upload FTP...");
    ESP_LOGI(TAG, "======================================");


    /* --------------------------------------------------------
     * 1. Verifica se a foto existe no SD
     * -------------------------------------------------------- */

    FILE *file = fopen(TEST_FILE, "rb");

    if (file == NULL)
    {
        ESP_LOGE(TAG, "Nao foi possivel abrir:");
        ESP_LOGE(TAG, "%s", TEST_FILE);

        return false;
    }


    fseek(file, 0, SEEK_END);

    long file_size = ftell(file);

    fseek(file, 0, SEEK_SET);

    fclose(file);


    ESP_LOGI(TAG, "Arquivo encontrado!");
    ESP_LOGI(TAG, "Arquivo: %s", TEST_FILE);
    ESP_LOGI(TAG, "Tamanho: %ld bytes", file_size);


    /* --------------------------------------------------------
     * 2. Cria cliente FTP
     * -------------------------------------------------------- */

    FtpClient *ftp = getFtpClient();

    if (ftp == NULL)
    {
        ESP_LOGE(TAG, "Falha ao criar cliente FTP!");

        return false;
    }


    /* --------------------------------------------------------
     * 3. Conecta ao servidor
     * -------------------------------------------------------- */

    ESP_LOGI(TAG, "Conectando ao servidor FTP...");

    int connect = ftp->ftpClientConnect(
        FTP_SERVER,
        FTP_PORT,
        &ftp_netbuf
    );


    if (connect == 0)
    {
        ESP_LOGE(TAG, "Falha ao conectar no FTP!");

        return false;
    }


    ESP_LOGI(TAG, "Conexao FTP estabelecida!");


    /* --------------------------------------------------------
     * 4. Login
     * -------------------------------------------------------- */

    ESP_LOGI(TAG, "Autenticando...");

    int login = ftp->ftpClientLogin(
        FTP_USER,
        FTP_PASSWORD,
        ftp_netbuf
    );


    if (login == 0)
    {
        ESP_LOGE(TAG, "Falha na autenticacao FTP!");

        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    ESP_LOGI(TAG, "Login FTP realizado!");

/* --------------------------------------------------------
 * 5. Criar / entrar na pasta raiz das cameras
 * -------------------------------------------------------- */

ESP_LOGI(TAG, "Verificando pasta raiz das cameras...");
ESP_LOGI(TAG, "Pasta: %s", CAMERA_ROOT_FOLDER);

int result = ftp->ftpClientChangeDir(
    CAMERA_ROOT_FOLDER,
    ftp_netbuf
);

if (result != 1)
{
    ESP_LOGW(
        TAG,
        "Pasta raiz nao existe. Criando..."
    );

    result = ftp->ftpClientMakeDir(
        CAMERA_ROOT_FOLDER,
        ftp_netbuf
    );

    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar a pasta: %s",
            CAMERA_ROOT_FOLDER
        );

        ftp->ftpClientQuit(ftp_netbuf);
        ftp_netbuf = NULL;

        return false;
    }

    ESP_LOGI(
        TAG,
        "Pasta raiz criada: %s",
        CAMERA_ROOT_FOLDER
    );

    result = ftp->ftpClientChangeDir(
        CAMERA_ROOT_FOLDER,
        ftp_netbuf
    );

    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel entrar na pasta: %s",
            CAMERA_ROOT_FOLDER
        );

        ftp->ftpClientQuit(ftp_netbuf);
        ftp_netbuf = NULL;

        return false;
    }
}

ESP_LOGI(
    TAG,
    "Dentro da pasta raiz: %s",
    CAMERA_ROOT_FOLDER
);


/* --------------------------------------------------------
 * 5.1 Criar / entrar na pasta do equipamento
 * -------------------------------------------------------- */

ESP_LOGI(TAG, "Verificando pasta do equipamento...");
ESP_LOGI(TAG, "Pasta: %s", DEVICE_NAME);

result = ftp->ftpClientChangeDir(
    DEVICE_NAME,
    ftp_netbuf
);

if (result != 1)
{
    ESP_LOGW(
        TAG,
        "Pasta do equipamento nao existe. Criando..."
    );

    result = ftp->ftpClientMakeDir(
        DEVICE_NAME,
        ftp_netbuf
    );

    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel criar a pasta: %s",
            DEVICE_NAME
        );

        ftp->ftpClientQuit(ftp_netbuf);
        ftp_netbuf = NULL;

        return false;
    }

    ESP_LOGI(
        TAG,
        "Pasta criada: %s",
        DEVICE_NAME
    );

    result = ftp->ftpClientChangeDir(
        DEVICE_NAME,
        ftp_netbuf
    );

    if (result != 1)
    {
        ESP_LOGE(
            TAG,
            "Nao foi possivel entrar na pasta: %s",
            DEVICE_NAME
        );

        ftp->ftpClientQuit(ftp_netbuf);
        ftp_netbuf = NULL;

        return false;
    }
}

ESP_LOGI(
    TAG,
    "Dentro da pasta: %s",
    DEVICE_NAME
);

    /* --------------------------------------------------------
     * 6. Criar / entrar na pasta do evento
     * -------------------------------------------------------- */

    char event_folder[32];

    snprintf(
        event_folder,
        sizeof(event_folder),
        "event_%06u",
        EVENT_ID
    );


    ESP_LOGI(
        TAG,
        "Verificando pasta do evento: %s",
        event_folder
    );


    result = ftp->ftpClientChangeDir(
        event_folder,
        ftp_netbuf
    );


    if (result != 1)
    {
        ESP_LOGW(
            TAG,
            "Pasta do evento nao existe. Criando..."
        );


        result = ftp->ftpClientMakeDir(
            event_folder,
            ftp_netbuf
        );


        if (result != 1)
        {
            ESP_LOGE(
                TAG,
                "Nao foi possivel criar: %s",
                event_folder
            );

            ftp->ftpClientQuit(ftp_netbuf);

            ftp_netbuf = NULL;

            return false;
        }


        ESP_LOGI(
            TAG,
            "Pasta criada: %s",
            event_folder
        );


        /* Entra na pasta criada */

        result = ftp->ftpClientChangeDir(
            event_folder,
            ftp_netbuf
        );


        if (result != 1)
        {
            ESP_LOGE(
                TAG,
                "Nao foi possivel entrar em: %s",
                event_folder
            );

            ftp->ftpClientQuit(ftp_netbuf);

            ftp_netbuf = NULL;

            return false;
        }
    }


    ESP_LOGI(
        TAG,
        "Dentro da pasta do evento: %s",
        event_folder
    );


    /* --------------------------------------------------------
     * 7. Upload da foto
     * -------------------------------------------------------- */

    ESP_LOGI(TAG, "Enviando arquivo...");
    ESP_LOGI(TAG, "Local : %s", TEST_FILE);
    ESP_LOGI(TAG, "Remoto: %s", REMOTE_FILE);


    result = ftp->ftpClientPut(
        TEST_FILE,
        REMOTE_FILE,
        FTP_CLIENT_BINARY,
        ftp_netbuf
    );


    if (result <= 0)
    {
        ESP_LOGE(TAG, "Falha ao enviar arquivo!");

        ftp->ftpClientQuit(ftp_netbuf);

        ftp_netbuf = NULL;

        return false;
    }


    /* --------------------------------------------------------
     * 8. Sucesso
     * -------------------------------------------------------- */

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "UPLOAD FTP CONCLUIDO!");
    ESP_LOGI(TAG, "Equipamento: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "Evento: %s", event_folder);
    ESP_LOGI(TAG, "Arquivo: %s", REMOTE_FILE);
    ESP_LOGI(TAG, "======================================");


    /* --------------------------------------------------------
     * 9. Fecha conexao
     * -------------------------------------------------------- */

    ftp->ftpClientQuit(ftp_netbuf);

    ftp_netbuf = NULL;


    return true;
}


/* ============================================================
 * TASK FTP
 * ============================================================ */

static void cloud_upload_test_task(void *arg)
{
    ESP_LOGI(TAG, "Task FTP iniciada");


    bool result = cloud_upload_test();


    if (result)
    {
        ESP_LOGI(
            TAG,
            "Teste FTP realizado com sucesso!"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Teste FTP falhou!"
        );
    }


    ESP_LOGI(TAG, "Task FTP finalizada");


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