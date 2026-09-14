#ifndef EVENT_CAMERA_H
#define EVENT_CAMERA_H

#include <stdbool.h>

/*
 * Inicializa o módulo de captura de eventos.
 */
void event_camera_init(void);

/*
 * Captura 10 fotos com intervalo de 500 ms.
 *
 * Por enquanto as fotos ficam na PSRAM apenas para teste.
 * Na próxima etapa vamos enviar cada frame para o SD.
 *
 * Retorna true se as 10 capturas forem realizadas.
 */
bool event_camera_capture_event(void);

#endif