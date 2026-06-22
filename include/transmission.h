#ifndef TRANSMISSAO_H
#define TRANSMISSAO_H

#include "protocol.h"

#include <stdint.h>

#define TIMEOUT_ACK_MS 3000

// Envia uma resposta de controle sem payload para confirmar ou negar um pacote.
int envia_ack_nack(int soquete, uint8_t tipo_resposta, uint8_t sequencia);

// Espera ACK ou NACK da sequencia informada ate estourar o timeout.
int espera_ack_nack_com_timeout(int soquete, uint8_t sequencia_esperada);

// Envia um pacote de dados e reenvia em caso de NACK ou timeout.
int envia_pacote_com_reenvio(int soquete, mensagem_t *mensagem,
                             uint8_t *proxima_sequencia);

// Envia um buffer grande, fragmentando em pacotes do protocolo e finalizando a transmissao.
int envia_buffer_protocolado(int soquete, uint8_t tipo_msg, const uint8_t *buffer,
                             size_t tamanho_buffer, uint8_t *proxima_sequencia);

// Calcula a proxima sequencia circular de 6 bits.
uint8_t calcula_proxima_sequencia(uint8_t sequencia);

// Calcula a sequencia anterior circular de 6 bits.
uint8_t calcula_sequencia_anterior(uint8_t sequencia);

// Extrai a sequencia de um pacote bruto, mesmo antes da validacao completa.
uint8_t extrai_sequencia_pacote_bruto(const uint8_t *pacote);

#endif
