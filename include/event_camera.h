#ifndef EVENT_CAMERA_H
#define EVENT_CAMERA_H

#include <stdbool.h>

/*
 * Inicializa o módulo de captura de eventos.
 */
void event_camera_init(void);

/*
 * Captura 10 fotos com intervalo de 500 ms
 * e salva as fotos no cartão SD.
 *
 * O ID do evento criado é retornado através
 * do ponteiro event_id.
 *
 * Retorna true se as 10 capturas forem
 * realizadas com sucesso.
 */
bool event_camera_capture_event(
    unsigned int *event_id
);

#endif