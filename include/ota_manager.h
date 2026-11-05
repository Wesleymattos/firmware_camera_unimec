
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

/*
 * Inicializa o módulo OTA.
 */
esp_err_t ota_manager_init(void);

/*
 * Solicita uma atualização de firmware.
 *
 * A URL deve apontar para o arquivo .bin
 * publicado no GitHub Releases.
 */
esp_err_t ota_manager_start(const char *firmware_url);

/*
 * Informa se uma atualização está em andamento.
 */
bool ota_manager_is_running(void);

#endif
