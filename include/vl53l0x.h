#ifndef VL53L0X_H
#define VL53L0X_H

#include <stdbool.h>
#include <stdint.h>

#define VL53L0X_I2C_PORT    I2C_NUM_0

#define VL53L0X_SDA_GPIO    2
#define VL53L0X_SCL_GPIO    14

#define VL53L0X_I2C_ADDRESS 0x29

bool vl53l0x_init(void);

bool vl53l0x_read_distance(uint16_t *distance_mm);

#endif