#ifndef DETECTOR_H
#define DETECTOR_H

#include <stdbool.h>

void detector_init(void);

/*
 * Retorna true quando uma pessoa for detectada.
 *
 * Por enquanto é uma função FICTÍCIA para testes.
 * Depois será substituída pelo módulo/sensor real.
 */
bool detector_pessoa_detectada(void);

#endif
