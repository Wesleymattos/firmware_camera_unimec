#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

/*
 * Inicializa o Wi-Fi.
 */
void wifi_manager_init(void);

/*
 * Retorna true quando o Wi-Fi estiver conectado
 * e o ESP32 tiver recebido um endereço IP.
 */
bool wifi_manager_is_connected(void);

#endif