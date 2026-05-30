#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

static void imprime_mensagem_protocolada(const mensagem_t *mensagem)
{
    if (mensagem == NULL)
    {
        return;
    }

    printf("[DEBUG] Tipo: %u\n", mensagem->tipo_msg);
    printf("[DEBUG] Sequencia: %u\n", mensagem->num_sequencia_msg);
    printf("[DEBUG] Tamanho dados: %u\n", mensagem->tamanho_dados);

    // Imprime o payload como texto quando possivel
    printf("[DEBUG] Dados: ");
    for (uint8_t i = 0; i < mensagem->tamanho_dados; i++)
    {
        unsigned char c = mensagem->dados[i];

        // Caracteres imprimiveis aparecem como texto normal
        if (c >= 32 && c <= 126)
        {
            putchar(c);
        }
        else
        {
            // Bytes nao imprimiveis aparecem em hexadecimal
            printf("\\x%02X", c);
        }
    }
    putchar('\n');
    // Garante que a saida apareça mesmo com o servidor em loop
    fflush(stdout);
}

static int envia_ack_nack(int soquete, uint8_t tipo_resposta, uint8_t sequencia)
{
    mensagem_t resposta;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    if (tipo_resposta != MSG_ACK && tipo_resposta != MSG_NACK)
    {
        fprintf(stderr,
                "[ERRO] Tipo de resposta de controle invalido: %u\n",
                tipo_resposta);
        return -1;
    }

    memset(&resposta, 0, sizeof(resposta));

    resposta.tipo_msg = tipo_resposta;
    resposta.num_sequencia_msg = sequencia;
    resposta.tamanho_dados = 0;

    // Monta o pacote com a mensagem (ack ou nack)
    if (monta_pacote(&resposta, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar resposta de controle\n");
        return -1;
    }

    // Envia o pacote de controle pela camada de rede
    ssize_t enviado = envia_mensagem(soquete, pacote, tamanho_pacote);

    if (enviado < 0)
    {
        perror("envia_mensagem resposta controle");
        return -1;
    }

    // Confere se todos os bytes do pacote foram enviados
    if ((size_t)enviado != tamanho_pacote)
    {
        fprintf(stderr,
                "[ERRO] Envio incompleto da resposta. Enviado: %zd, esperado: %zu\n",
                enviado,
                tamanho_pacote);
        return -1;
    }

    if (tipo_resposta == MSG_ACK)
    {
        printf("[DEBUG] ACK enviado para seq=%u\n", sequencia);
    }
    else
    {
        printf("[DEBUG] NACK enviado para seq=%u\n", sequencia);
    }

    return 0;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

int executa_servidor(int soquete)
{
    // Buffer que recebe um pacote PacMan ja sem cabecalho Ethernet
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;

    printf("Servidor aguardando pacotes do protocolo PacMan...\n");

    while (1)
    {
        // Bloqueia ate chegar um pacote PacMan valido na camada de rede
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        // Trata erro de recebimento sem encerrar em interrupcoes de sinal
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        // Mensagem vazia não forma msg para o protocolo
        if (recebido == 0)
        {
            continue;
        }

        // Valida e transforma o pacote bruto em uma mensagem do protocolo
        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido\n");
            continue;
        }

        // Mostra no terminal a mensagem recebida
        imprime_mensagem_protocolada(&mensagem);

        // Confirma o recebimento da msg
        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);

        // DEBUG Quando houver validacao de CRC, pacotes corrompidos devem receber NACK
    }
}
