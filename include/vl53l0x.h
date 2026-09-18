#ifndef VL53L0X_H
#define VL53L0X_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================
 * CONFIGURACAO I2C
 * ============================================================ */

#define VL53L0X_I2C_PORT        I2C_NUM_0

#define VL53L0X_SDA_GPIO        1
#define VL53L0X_SCL_GPIO        2

#define VL53L0X_I2C_ADDRESS     0x29


/* ============================================================
 * FUNCOES PUBLICAS
 * ============================================================ */

/**
 * Inicializa o barramento I2C e o VL53L0X.
 *
 * Retorna true se o sensor foi encontrado e inicializado.
 */
bool vl53l0x_init(void);


/**
 * Realiza uma leitura de distancia.
 *
 * distance_mm:
 * Ponteiro onde sera armazenada a distancia em milimetros.
 *
 * Retorna true se a leitura foi realizada com sucesso.
 */
bool vl53l0x_read_distance(uint16_t *distance_mm);

#endif