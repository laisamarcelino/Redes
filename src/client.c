#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TIMEOUT_ACK_MS 1000
#define MAX_TENTATIVAS_ENVIO 5

static int espera_ack_nack_com_timeout(int soquete, uint8_t sequencia_esperada);

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

/* APAGAR
 * 
 * Fluxo para-e-espera
 * envia pacote
 * espera ACK/NACK
 * se ACK: confirma
 * se NACK: reenvia
 * se timeout: reenvia
*/
static int envia_mensagem_protocolada(int soquete, const char *texto)
{
    static uint8_t proxima_sequencia = 0;

    mensagem_t mensagem;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;
    size_t tamanho_texto;

    int tentativa;

    if (texto == NULL || texto[0] == '\0')
    {
        fprintf(stderr, "[ERRO] Texto nulo ou vazio\n");
        return -1;
    }

    tamanho_texto = strlen(texto);

    if (tamanho_texto > TAMANHO_MAX_DADOS)
    {
        fprintf(stderr,
                "[ERRO] Mensagem grande demais para um pacote. Maximo: %d bytes\n",
                TAMANHO_MAX_DADOS);
        return -1;
    }

    memset(&mensagem, 0, sizeof(mensagem));

    mensagem.tipo_msg = MSG_DADOS;
    mensagem.num_sequencia_msg = proxima_sequencia;
    mensagem.tamanho_dados = (uint8_t)tamanho_texto;

    memcpy(mensagem.dados, texto, tamanho_texto);

    if (monta_pacote(&mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return -1;
    }

    for (tentativa = 1; tentativa <= MAX_TENTATIVAS_ENVIO; tentativa++)
    {
        printf("[DEBUG] Enviando pacote seq=%u, tentativa %d/%d\n",
               mensagem.num_sequencia_msg,
               tentativa,
               MAX_TENTATIVAS_ENVIO);

        ssize_t enviado = envia_mensagem(soquete, pacote, tamanho_pacote);

        if (enviado < 0)
        {
            if (errno == EINTR)
            {
                tentativa--;
                continue;
            }

            perror("envia_mensagem");
            return -1;
        }

        if ((size_t)enviado != tamanho_pacote)
        {
            fprintf(stderr,
                    "[ERRO] Envio incompleto. Enviado: %zd, esperado: %zu\n",
                    enviado,
                    tamanho_pacote);
            return -1;
        }

        int resposta = espera_ack_nack_com_timeout(
            soquete,
            mensagem.num_sequencia_msg);

        if (resposta == MSG_ACK)
        {
            printf("[DEBUG] Envio confirmado com ACK seq=%u\n",
                   mensagem.num_sequencia_msg);

            proxima_sequencia =
                (uint8_t)((proxima_sequencia + 1) % (SEQUENCIA_MAX + 1));

            return 0;
        }

        if (resposta == MSG_NACK)
        {
            fprintf(stderr,
                    "[DEBUG] Servidor retornou NACK seq=%u. Reenviando...\n",
                    mensagem.num_sequencia_msg);
            continue;
        }

        if (resposta == REDE_TIMEOUT)
        {
            fprintf(stderr,
                    "[DEBUG] Timeout seq=%u. Reenviando...\n",
                    mensagem.num_sequencia_msg);
            continue;
        }

        fprintf(stderr,
                "[ERRO] Falha ao esperar resposta do servidor\n");
        return -1;
    }

    fprintf(stderr,
            "[ERRO] Numero maximo de tentativas atingido para seq=%u\n",
            mensagem.num_sequencia_msg);

    return -1;
}

static int espera_ack_nack_com_timeout(int soquete, uint8_t sequencia_esperada)
{
    uint8_t pacote_resposta[TAMANHO_MAX_PACOTE];
    mensagem_t resposta;

    while (1)
    {
        ssize_t recebido = espera_mensagem_timeout(
            soquete,
            pacote_resposta,
            sizeof(pacote_resposta),
            TIMEOUT_ACK_MS);

        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr,
                    "[DEBUG] Timeout esperando ACK/NACK seq=%u\n",
                    sequencia_esperada);
            return REDE_TIMEOUT;
        }

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_timeout");
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(
                pacote_resposta,
                (size_t)recebido,
                &resposta) != 0)
        {
            fprintf(stderr, "[ERRO] Cliente recebeu pacote invalido\n");
            return -1;
        }

        if (resposta.num_sequencia_msg != sequencia_esperada)
        {
            fprintf(stderr,
                    "[DEBUG] Resposta ignorada. Esperado seq=%u, recebido seq=%u\n",
                    sequencia_esperada,
                    resposta.num_sequencia_msg);
            continue;
        }

        if (resposta.tipo_msg == MSG_ACK)
        {
            printf("[DEBUG] ACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_ACK;
        }

        if (resposta.tipo_msg == MSG_NACK)
        {
            printf("[DEBUG] NACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_NACK;
        }

        fprintf(stderr,
                "[DEBUG] Tipo de resposta ignorado: %u\n",
                resposta.tipo_msg);
    }
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

int executa_cliente(int soquete, const char *mensagem)
{
    // Valida a mensagem recebida pela linha de comando
    if (mensagem == NULL || mensagem[0] == '\0')
    {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    // Envia a mensagem e trata ACK, NACK e timeout.
    return envia_mensagem_protocolada(soquete, mensagem);
}
