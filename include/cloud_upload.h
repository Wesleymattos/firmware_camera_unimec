#ifndef CLOUD_UPLOAD_H
#define CLOUD_UPLOAD_H

#include <stdbool.h>

/*
 * Inicializa o módulo FTP.
 */
void cloud_upload_init(void);


/*
 * Função de teste.
 *
 * Mantida temporariamente para testes.
 */
bool cloud_upload_test(void);


/*
 * Inicia uma task própria para enviar
 * todas as fotos de um evento.
 *
 * Exemplo:
 *
 * cloud_upload_start_event(5);
 *
 * A task FTP ficará responsável por
 * enviar as 10 fotos do evento 000005.
 */
void cloud_upload_start_event(
    unsigned int event_id
);


/*
 * Função que realiza o upload completo
 * de um evento.
 */
bool cloud_upload_event(
    unsigned int event_id
);


/*
 * Inicia a task de teste FTP.
 *
 * Mantida temporariamente.
 */
void cloud_upload_start_test(void);

#endif