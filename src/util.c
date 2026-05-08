#include "../include/util.h"

// Aloca um buffer para envio e recebimento de quadros na camada de enlace
unsigned char* aloca_buffer_raw(void) {
    return calloc(TAM_BUFFER_RAW, sizeof(unsigned char));
}

// Libera o buffer alocado por aloca_buffer_raw()
void libera_buffer_raw(unsigned char *buffer) {
    free(buffer);
}
