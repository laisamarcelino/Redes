#ifndef TRANSMISSAO_H
#define TRANSMISSAO_H

#include "protocol.h"

#include <stdint.h>

#define MAX_TENTATIVAS_REENVIO 10
#define TIMEOUT_ESPERA_RESPOSTA_MS 3000

int envia_ack_nack(int soquete, uint8_t tipo_resposta, uint8_t sequencia);

int espera_ack_nack(int *p_soquete, uint8_t sequencia_esperada);

// Queda de cabo não conta como tentativa; NACK e timeout contam.
int envia_pacote_com_reenvio(int *p_soquete, mensagem_t *mensagem,
                             uint8_t *proxima_sequencia);

// Buffers maiores que 31 bytes são fragmentados e fechados com FIM_TRANSMISSAO.
int envia_buffer_protocolado(int *p_soquete, uint8_t tipo_msg, const uint8_t *buffer,
                             size_t tamanho_buffer, uint8_t *proxima_sequencia);

uint8_t calcula_proxima_sequencia(uint8_t sequencia);

uint8_t calcula_sequencia_anterior(uint8_t sequencia);

// Usado para enviar NACK mesmo quando o pacote falha na validação completa.
uint8_t extrai_sequencia_pacote_bruto(const uint8_t *pacote);

#endif
