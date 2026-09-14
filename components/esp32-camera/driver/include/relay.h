#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>

void relay_init(void);

void relay_on(void);

void relay_off(void);

void relay_set(bool state);

void relay_toggle(void);

bool relay_get_state(void);

#endif