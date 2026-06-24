#ifndef ARQUIVOS_H
#define ARQUIVOS_H

#include <stdint.h>

uint8_t tipo_arquivo_por_caminho(const char *caminho);

int envia_arquivo_protocolado(int *p_soquete, const char *caminho_arquivo,
                              uint8_t tipo_msg, uint8_t *proxima_sequencia);

#endif
