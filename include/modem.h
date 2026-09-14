#ifndef MODEM_H
#define MODEM_H

#include <stdbool.h>

void modem_init(void);

/*
 * Retorna true quando a interface PPP estiver conectada
 * e houver uma conexão IP disponível.
 */
bool modem_is_connected(void);

/*
 * Processa o modem.
 *
 * A implementação real será adaptada do seu código
 * existente do SIM7080G + PPP.
 */
void modem_process(void);

#endif
