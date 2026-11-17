
#ifndef FTP_CLIENT_H
#define FTP_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

typedef struct
{
    int control_socket;
    int data_socket;
    char server_ip[48];
    uint16_t server_port;
    int connected;
    int logged_in;
} ftp_client_t;

esp_err_t ftp_client_init(ftp_client_t *client);

esp_err_t ftp_client_connect(
    ftp_client_t *client,
    const char *server_ip,
    uint16_t server_port
);

esp_err_t ftp_client_login(
    ftp_client_t *client,
    const char *username,
    const char *password
);

esp_err_t ftp_client_open_passive(ftp_client_t *client);

esp_err_t ftp_client_store_file(
    ftp_client_t *client,
    const char *local_path,
    const char *remote_path
);


esp_err_t ftp_client_quit(ftp_client_t *client);

esp_err_t ftp_client_change_dir(
    ftp_client_t *client,
    const char *folder
);

esp_err_t ftp_client_make_dir(
    ftp_client_t *client,
    const char *folder
);

#endif
