
#ifndef MODEM_Y7080E_H
#define MODEM_Y7080E_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t modem_y7080e_init(void);

void modem_y7080e_reset(void);

bool modem_y7080e_is_connected(void);

#endif
