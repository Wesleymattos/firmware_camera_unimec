#ifndef LED_STATUS_H
#define LED_STATUS_H


#include <stdint.h>

void led_status_init(void);

void led_status_set_color(
    uint8_t red,
    uint8_t green,
    uint8_t blue
);




void led_status_relay_on(void);
void led_status_relay_off(void);

/* Estado visual durante atualizacao OTA */
void led_status_ota_start(void);



#endif