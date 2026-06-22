#ifndef ARQUIVOS_H
#define ARQUIVOS_H

#include <stdint.h>

// Retorna o tipo de mensagem usado para a extensao do arquivo.
uint8_t tipo_arquivo_por_caminho(const char *caminho);

// Envia um arquivo em blocos de ate TAMANHO_MAX_DADOS bytes.
int envia_arquivo_protocolado(int *p_soquete, const char *caminho_arquivo,
                              uint8_t tipo_msg, uint8_t *proxima_sequencia);

// Recebe um arquivo do tipo esperado e grava no caminho informado.
int recebe_arquivo_protocolado(int *p_soquete, const char *caminho_saida,
                               uint8_t tipo_esperado, uint8_t *sequencia_esperada);

#endif
