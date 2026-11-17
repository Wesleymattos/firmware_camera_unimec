
#include "ftp_client.h"
#include "modem_y7080e.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FTP_CLIENT";

#define FTP_RESPONSE_SIZE 1024
#define FTP_TIMEOUT_MS    20000


static esp_err_t ftp_read_response(
    ftp_client_t *client,
    char *response,
    size_t response_size,
    int *response_code)
{
    if (client == NULL || response == NULL ||
        response_size < 8 || response_code == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(response, 0, response_size);

    size_t total = 0;
    int first_code = -1;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(FTP_TIMEOUT_MS);

    while ((xTaskGetTickCount() - start) < timeout)
    {
        uint8_t buffer[256];

        int received = modem_y7080e_socket_receive(
            client->control_socket,
            buffer,
            sizeof(buffer)
        );

        if (received < 0)
            return ESP_FAIL;

        if (received == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (total + received >= response_size)
            return ESP_ERR_INVALID_SIZE;

        memcpy(response + total, buffer, received);
        total += received;
        response[total] = '\0';

        char *line = response;

        while (true)
        {
            char *end = strstr(line, "\r\n");

            if (end == NULL)
                break;

            size_t line_length = end - line;

            if (line_length >= 4 &&
                line[0] >= '0' && line[0] <= '9' &&
                line[1] >= '0' && line[1] <= '9' &&
                line[2] >= '0' && line[2] <= '9')
            {
                int code = (line[0] - '0') * 100 +
                           (line[1] - '0') * 10 +
                           (line[2] - '0');

                if (first_code < 0)
                {
                    first_code = code;

                    if (line[3] == ' ')
                    {
                        *response_code = code;
                        return ESP_OK;
                    }

                    if (line[3] != '-')
                        return ESP_FAIL;
                }
                else if (code == first_code && line[3] == ' ')
                {
                    *response_code = code;
                    return ESP_OK;
                }
            }

            line = end + 2;
        }
    }

    ESP_LOGE(TAG, "Timeout aguardando resposta FTP");

    return ESP_ERR_TIMEOUT;
}


static esp_err_t ftp_send_command(
    ftp_client_t *client,
    const char *command,
    int *response_code)
{
    char response[FTP_RESPONSE_SIZE];
    char command_line[512];

    int length = snprintf(
        command_line,
        sizeof(command_line),
        "%s\r\n",
        command
    );

    if (length < 0 || length >= sizeof(command_line))
        return ESP_ERR_INVALID_SIZE;

    esp_err_t err = modem_y7080e_socket_send(
        client->control_socket,
        (const uint8_t *)command_line,
        length
    );

    if (err != ESP_OK)
        return err;

    return ftp_read_response(
        client,
        response,
        sizeof(response),
        response_code
    );
}

esp_err_t ftp_client_init(ftp_client_t *client)
{
    if (client == NULL)
        return ESP_ERR_INVALID_ARG;

    memset(client, 0, sizeof(*client));

    client->control_socket = -1;
    client->data_socket = -1;

    ESP_LOGI(TAG, "Cliente FTP inicializado");

    return ESP_OK;
}

esp_err_t ftp_client_connect(
    ftp_client_t *client,
    const char *server_ip,
    uint16_t server_port)
{
    if (client == NULL || server_ip == NULL ||
        server_port == 0)
        return ESP_ERR_INVALID_ARG;

    if (client->connected)
        return ESP_ERR_INVALID_STATE;

    strncpy(
        client->server_ip,
        server_ip,
        sizeof(client->server_ip) - 1
    );

    client->server_port = server_port;

    esp_err_t err = modem_y7080e_socket_open(
        &client->control_socket
    );

    if (err != ESP_OK)
        return err;

    err = modem_y7080e_socket_connect(
        client->control_socket,
        client->server_ip,
        client->server_port
    );

    if (err != ESP_OK)
    {
        modem_y7080e_socket_close(client->control_socket);
        client->control_socket = -1;
        return err;
    }

    char response[FTP_RESPONSE_SIZE];
    int response_code = 0;

    err = ftp_read_response(
        client,
        response,
        sizeof(response),
        &response_code
    );

    if (err != ESP_OK || response_code != 220)
    {
        modem_y7080e_socket_close(client->control_socket);
        client->control_socket = -1;

        return err != ESP_OK ? err : ESP_FAIL;
    }

    client->connected = 1;

    ESP_LOGI(TAG, "Conectado ao servidor FTP");

    return ESP_OK;
}

esp_err_t ftp_client_login(
    ftp_client_t *client,
    const char *username,
    const char *password)
{
    if (client == NULL || username == NULL || password == NULL)
        return ESP_ERR_INVALID_ARG;

    if (!client->connected)
        return ESP_ERR_INVALID_STATE;

    int response_code = 0;

    char command[256];

    snprintf(command, sizeof(command), "USER %s", username);

    esp_err_t err = ftp_send_command(
        client,
        command,
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code == 230)
    {
        client->logged_in = 1;
        return ESP_OK;
    }

    if (response_code != 331)
        return ESP_FAIL;

    snprintf(command, sizeof(command), "PASS %s", password);

    err = ftp_send_command(
        client,
        command,
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code != 230)
        return ESP_FAIL;

    client->logged_in = 1;

    ESP_LOGI(TAG, "Autenticacao FTP concluida");

    return ESP_OK;
}



/* ============================================================
 * NAVEGACAO E CRIACAO DE DIRETORIOS FTP
 * ============================================================ */

esp_err_t ftp_client_change_dir(
    ftp_client_t *client,
    const char *folder
)
{
    if (client == NULL || folder == NULL || !client->logged_in)
        return ESP_ERR_INVALID_ARG;

    char command[256];
    int response_code = 0;

    snprintf(command, sizeof(command), "CWD %s", folder);

    esp_err_t err = ftp_send_command(
        client,
        command,
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code != 250)
    {
        ESP_LOGE(TAG, "Falha ao entrar na pasta %s: FTP %d",
                 folder, response_code);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Diretorio acessado: %s", folder);
    return ESP_OK;
}


esp_err_t ftp_client_make_dir(
    ftp_client_t *client,
    const char *folder
)
{
    if (client == NULL || folder == NULL || !client->logged_in)
        return ESP_ERR_INVALID_ARG;

    char command[256];
    int response_code = 0;

    snprintf(command, sizeof(command), "MKD %s", folder);

    esp_err_t err = ftp_send_command(
        client,
        command,
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code != 257)
    {
        ESP_LOGE(TAG, "Falha ao criar a pasta %s: FTP %d",
                 folder, response_code);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Diretorio criado: %s", folder);
    return ESP_OK;
}


esp_err_t ftp_client_open_passive(ftp_client_t *client)
{
    if (client == NULL || !client->connected || !client->logged_in)
        return ESP_ERR_INVALID_STATE;

    if (client->data_socket >= 0)
        return ESP_ERR_INVALID_STATE;

    char command[] = "PASV\r\n";
    char response[FTP_RESPONSE_SIZE];
    int response_code = 0;

    // Envia o comando PASV pelo socket de controle
    esp_err_t err = modem_y7080e_socket_send(
        client->control_socket,
        (const uint8_t *)command,
        strlen(command)
    );

    if (err != ESP_OK)
        return err;

    // Aguarda a resposta 227 do servidor
    err = ftp_read_response(
        client,
        response,
        sizeof(response),
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code != 227)
    {
        ESP_LOGE(TAG, "Servidor nao aceitou PASV: %d", response_code);
        return ESP_FAIL;
    }

    // Extrai os seis valores da resposta PASV
    int h1, h2, h3, h4, p1, p2;

    char *start = strchr(response, '(');

    if (start == NULL ||
        sscanf(start, "(%d,%d,%d,%d,%d,%d)",
               &h1, &h2, &h3, &h4, &p1, &p2) != 6)
    {
        ESP_LOGE(TAG, "Resposta PASV invalida: %s", response);
        return ESP_FAIL;
    }

    if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 ||
        h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0 ||
        p1 < 0 || p1 > 255 || p2 < 0 || p2 > 255)
    {
        ESP_LOGE(TAG, "Parametros PASV invalidos");
        return ESP_FAIL;
    }

    uint16_t data_port = (uint16_t)(p1 * 256 + p2);

    if (data_port == 0)
        return ESP_FAIL;

    // Usa o IP da conexao de controle para evitar problemas de NAT.
    // O servidor pode anunciar um IP privado na resposta PASV.
    ESP_LOGI(TAG,
             "PASV: IP anunciado %d.%d.%d.%d, porta %u",
             h1, h2, h3, h4, data_port);

    // Abre o segundo socket TCP
    err = modem_y7080e_socket_open(&client->data_socket);

    if (err != ESP_OK)
    {
        client->data_socket = -1;
        return err;
    }

    // Conecta ao canal de dados do servidor
    err = modem_y7080e_socket_connect(
        client->data_socket,
        client->server_ip,
        data_port
    );

    if (err != ESP_OK)
    {
        modem_y7080e_socket_close(client->data_socket);
        client->data_socket = -1;
        return err;
    }

    ESP_LOGI(TAG, "Canal FTP de dados conectado na porta %u", data_port);

    return ESP_OK;
}


esp_err_t ftp_client_store_file(
    ftp_client_t *client,
    const char *local_path,
    const char *remote_path)
{
    if (client == NULL || local_path == NULL || remote_path == NULL)
        return ESP_ERR_INVALID_ARG;

    if (!client->connected || !client->logged_in ||
        client->data_socket < 0)
        return ESP_ERR_INVALID_STATE;

    FILE *file = fopen(local_path, "rb");

    if (file == NULL)
    {
        ESP_LOGE(TAG, "Nao foi possivel abrir: %s", local_path);
        return ESP_FAIL;
    }

    int response_code = 0;
    esp_err_t err;

    // Define transferencia em modo binario
    err = ftp_send_command(client, "TYPE I", &response_code);

    if (err != ESP_OK || response_code != 200)
    {
        fclose(file);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    // Solicita o armazenamento do arquivo no servidor
    char command[512];

    int length = snprintf(
        command,
        sizeof(command),
        "STOR %s",
        remote_path
    );

    if (length < 0 || length >= sizeof(command))
    {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }

    err = ftp_send_command(client, command, &response_code);

    if (err != ESP_OK ||
        (response_code != 125 && response_code != 150))
    {
        fclose(file);
        return err != ESP_OK ? err : ESP_FAIL;
    }

    // Transfere o arquivo em blocos, sem carregar tudo na RAM
    uint8_t buffer[512];
    size_t total_sent = 0;

    while (1)
    {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);

        if (bytes_read == 0)
        {
            if (ferror(file))
                err = ESP_FAIL;

            break;
        }

        err = modem_y7080e_socket_send(
            client->data_socket,
            buffer,
            bytes_read
        );

        if (err != ESP_OK)
            break;

        total_sent += bytes_read;
    }

    fclose(file);

    // Encerra o canal de dados
    esp_err_t close_err = modem_y7080e_socket_close(
        client->data_socket
    );

    client->data_socket = -1;

    if (err != ESP_OK)
        return err;

    if (close_err != ESP_OK)
        return close_err;

    // Aguarda a confirmacao final do servidor
    char response[FTP_RESPONSE_SIZE];

    err = ftp_read_response(
        client,
        response,
        sizeof(response),
        &response_code
    );

    if (err != ESP_OK)
        return err;

    if (response_code != 226 && response_code != 250)
    {
        ESP_LOGE(TAG, "Servidor nao confirmou STOR: %d", response_code);
        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Arquivo enviado: %s (%u bytes)",
        remote_path,
        (unsigned int)total_sent
    );

    return ESP_OK;
}






esp_err_t ftp_client_quit(ftp_client_t *client)
{
    if (client == NULL)
        return ESP_ERR_INVALID_ARG;

    if (client->data_socket >= 0)
    {
        modem_y7080e_socket_close(client->data_socket);
        client->data_socket = -1;
    }

    if (client->control_socket >= 0)
    {
        if (client->connected)
        {
            int response_code;
            ftp_send_command(client, "QUIT", &response_code);
        }

        modem_y7080e_socket_close(client->control_socket);
        client->control_socket = -1;
    }

    client->connected = 0;
    client->logged_in = 0;

    ESP_LOGI(TAG, "Cliente FTP encerrado");

    return ESP_OK;
}
