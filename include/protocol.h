#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

#define MARCADOR_INICIO 0x7E // *01111110 em hexadecimal

#define TAMANHO_MAX_DADOS 31 // *5 bits: valores de 0 a 31
#define SEQUENCIA_MAX 63     // *6 bits: valores de 0 a 63
#define TIPO_MAX 31          // *5 bits: valores de 0 a 31

#define TAMANHO_CABECALHO_PROTOCOLO 3 // *8+5+6+5 bits = 24 bits = 3 bytes
#define TAMANHO_CRC_PROTOCOLO 1 // ALTERAR

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
    MSG_ERRO = 15,
    MSG_FIM_TRANSMISSAO = 16
} tipo_msg_t;

/*
 * Cabecalho do protocolo PacMan.
 *
 * Formato serializado do pacote:
 *   [marcador_inicio:8][tamanho_dados:5][sequencia:6][tipo:5][dados...][crc]
 *
 * Como 8 + 5 + 6 + 5 = 24 bits, o cabecalho ocupa 3 bytes.
 *
 * Distribuicao dos bits:
 *   byte 0: marcador_inicio
 *   byte 1: tamanho_dados nos 5 bits mais altos + 3 bits mais altos da sequencia
 *   byte 2: 3 bits mais baixos da sequencia + tipo nos 5 bits mais baixos
 *
 * marcador_inicio:
 *   Deve ser sempre 01111110, representado por MARCADOR_INICIO (0x7E).
 *
 * tamanho_dados:
 *   Quantidade de bytes no campo dados. Como o campo tem 5 bits, aceita 0..31.
 * 
 * sequencia:
 *   Numero de sequencia usado pelo controle para-e-espera e pelos ACK/NACK.
 *   Como o campo tem 6 bits, aceita 0..63.
 *
 * tipo:
 *   Codigo da mensagem, usando os valores de tipo_msg_t. Como o campo tem
 *   5 bits, aceita 0..31.
 */
typedef struct
{
    uint8_t byte0; // marcador_inicio
    uint8_t byte1; // tamanho_dados + parte alta da sequencia
    uint8_t byte2; // parte baixa da sequencia + tipo
} cabecalho_protocolo_t;

int monta_pacote(
    uint8_t tipo,
    uint8_t sequencia,
    const uint8_t *dados,
    uint8_t tamanho_dados,
    uint8_t *saida,
    size_t capacidade_saida,
    size_t *tamanho_saida);

int desmonta_pacote(
    const uint8_t *entrada,
    size_t tamanho_entrada,
    uint8_t *tipo,
    uint8_t *sequencia,
    uint8_t *dados,
    uint8_t *tamanho_dados);

// * Valida se o pacote parece ser válido com base no protocolo definido
int valida_pacote(
    const uint8_t *entrada,
    size_t tamanho_entrada);

#endif
