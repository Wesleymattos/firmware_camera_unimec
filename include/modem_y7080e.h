
#ifndef MODEM_Y7080E_H
#define MODEM_Y7080E_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t modem_y7080e_init(void);

void modem_y7080e_reset(void);

bool modem_y7080e_is_connected(void);

esp_err_t modem_y7080e_socket_open(int *socket_id);

esp_err_t modem_y7080e_socket_connect(
    int socket_id,
    const char *server_ip,
    uint16_t server_port
);

esp_err_t modem_y7080e_socket_send(
    int socket_id,
    const uint8_t *data,
    size_t length
);

int modem_y7080e_socket_receive(
    int socket_id,
    uint8_t *buffer,
    size_t buffer_size
);


size_t modem_y7080e_socket_available(int socket_id);

esp_err_t modem_y7080e_socket_close(int socket_id);


esp_err_t modem_y7080e_mqtt_publish(
    const char *topic,
    const char *message
);

void modem_y7080e_process_mqtt(void);

#endif
