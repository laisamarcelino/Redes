#ifndef UTIL_H
#define UTIL_H

#include <stdlib.h>

#define TAM_BUFFER_RAW 2048

// Aloca um buffer para envio e recebimento de quadros na camada de enlace
unsigned char* aloca_buffer_raw(void);

// Libera o buffer alocado por aloca_buffer_raw()
void libera_buffer_raw(unsigned char *buffer);

#endif
