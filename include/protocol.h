#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

#define MARCADOR_INICIO 0x7E // 01111110 em hexadecimal

#define TAMANHO_MAX_DADOS 31 // 5 bits: valores de 0 a 31
#define SEQUENCIA_MAX 63     // 6 bits: valores de 0 a 63
#define TIPO_MAX 31          // 5 bits: valores de 0 a 31

#define TAMANHO_CABECALHO_PROTOCOLO 3 // 8+5+6+5 bits = 24 bits = 3 bytes
#define TAMANHO_CRC_PROTOCOLO 1       // 8 bits = 1 byte

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
    MSG_FIM_JOGO = 8,   // payload[0]: 0=continua 1=vitoria 2=derrota
    MSG_ERRO = 15,
    MSG_FIM_TRANSMISSAO = 16
} tipo_msg_t;

/*
 * Representacao logica da mensagem dentro do programa.
 * Essa struct nao é enviada pela rede.
 * Apenas facilita o acesso aos bits do pacote.
 */
typedef struct
{
    uint8_t tipo_msg;
    uint8_t num_sequencia_msg;
    uint8_t tamanho_dados;
    uint8_t dados[TAMANHO_MAX_DADOS];
} mensagem_t;

/*
 * Formato do pacote enviado pela rede:
 *
 * [marcador_inicio:8][tamanho_dados:5][sequencia:6][tipo:5][dados...][crc:8]
 *
 * Tamanho maximo:
 *   3 bytes de cabecalho + 31 bytes de dados + 1 byte de CRC = 35 bytes
 *
 * Distribuicao:
 *   pacote[0] = marcador_inicio
 *
 *   pacote[1] = tamanho_dados nos 5 bits mais altos
 *             + 3 bits mais altos da sequencia
 *
 *   pacote[2] = 3 bits mais baixos da sequencia
 *             + tipo nos 5 bits mais baixos
 *
 *   pacote[3...] = dados
 *   pacote[tamanho_total - 1] = crc
 */
int monta_pacote(const mensagem_t *mensagem, uint8_t pacote[TAMANHO_MAX_PACOTE],
                 size_t *tamanho_pacote);

int desmonta_pacote(const uint8_t *pacote, size_t tamanho_pacote,
                    mensagem_t *mensagem);

int valida_pacote(const uint8_t *pacote, size_t tamanho_pacote);

// Usado apenas para testes
void imprime_pacote(const uint8_t *pacote, size_t tamanho_pacote);

#endif