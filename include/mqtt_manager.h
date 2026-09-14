#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>

void mqtt_manager_init(void);

bool mqtt_manager_is_connected(void);

bool mqtt_manager_publish_alert(void);

#endif