#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

#define MARCADOR_INICIO 0x7E

#define TAMANHO_MAX_DADOS 31
#define SEQUENCIA_MAX 63
#define TIPO_MAX 31

#define TAMANHO_CABECALHO_PROTOCOLO 3
#define TAMANHO_CRC_PROTOCOLO 1

#define TAMANHO_MAX_PACOTE \
    (TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_MAX_DADOS + TAMANHO_CRC_PROTOCOLO)

typedef enum
{
    MSG_ACK = 0,
    MSG_NACK = 1,
    MSG_VISUALIZACAO = 2,
    MSG_INICIALIZACAO = 3,
    MSG_DADOS = 4,
    MSG_TXT = 5,
    MSG_JPG = 6,
    MSG_MP4 = 7,
    MSG_MOV_DIREITA = 10,
    MSG_MOV_ESQUERDA = 11,
    MSG_MOV_CIMA = 12,
    MSG_MOV_BAIXO = 13,
    MSG_FIM_JOGO = 8,
    MSG_ERRO = 15,
    MSG_FIM_TRANSMISSAO = 16
} tipo_msg_t;

/* A struct é uma representação interna; na rede segue apenas o pacote compactado. */
typedef struct
{
    uint8_t tipo_msg;
    uint8_t num_sequencia_msg;
    uint8_t tamanho_dados;
    uint8_t dados[TAMANHO_MAX_DADOS];
} mensagem_t;

/*
 * Formato definido para a rede:
 * [marcador:8][tamanho:5][sequencia:6][tipo:5][dados...][crc:8]
 */
int monta_pacote(const mensagem_t *mensagem, uint8_t pacote[TAMANHO_MAX_PACOTE],
                 size_t *tamanho_pacote);

int desmonta_pacote(const uint8_t *pacote, size_t tamanho_pacote,
                    mensagem_t *mensagem);

int valida_pacote(const uint8_t *pacote, size_t tamanho_pacote);

void imprime_pacote(const uint8_t *pacote, size_t tamanho_pacote);

#endif
