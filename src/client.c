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
static uint8_t proxima_sequencia = 0;

// Envia uma unica mensagem de tamanho maximo 31 bytes
static int envia_pacote_com_reenvio(int soquete, mensagem_t *mensagem)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    /*
     * A sequência é definida aqui.
     * Em caso de reenvio, a mesma sequência é mantida.
     */
    mensagem->num_sequencia_msg = proxima_sequencia;

    if (monta_pacote(mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return -1;
    }

    for (int tentativa = 1; tentativa <= MAX_TENTATIVAS_ENVIO; tentativa++)
    {
        uint8_t pacote_envio[TAMANHO_MAX_PACOTE];

        /*
         * Enviamos uma cópia.
         * Isso ajuda nos testes de corrupção e evita alterar o pacote original.
         */
        memcpy(pacote_envio, pacote, tamanho_pacote);

        printf("[DEBUG] Enviando pacote tipo=%u seq=%u tam=%u tentativa %d/%d\n",
               mensagem->tipo_msg,
               mensagem->num_sequencia_msg,
               mensagem->tamanho_dados,
               tentativa,
               MAX_TENTATIVAS_ENVIO);

        ssize_t enviado = envia_mensagem(
            soquete,
            pacote_envio,
            tamanho_pacote);

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
            mensagem->num_sequencia_msg);

        if (resposta == MSG_ACK)
        {
            printf("[DEBUG] ACK recebido para seq=%u\n",
                   mensagem->num_sequencia_msg);

            proxima_sequencia =
                (uint8_t)((proxima_sequencia + 1) % (SEQUENCIA_MAX + 1));

            return 0;
        }

        if (resposta == MSG_NACK)
        {
            fprintf(stderr,
                    "[DEBUG] NACK recebido para seq=%u. Reenviando...\n",
                    mensagem->num_sequencia_msg);
            continue;
        }

        if (resposta == REDE_TIMEOUT)
        {
            fprintf(stderr,
                    "[DEBUG] Timeout esperando resposta da seq=%u. Reenviando...\n",
                    mensagem->num_sequencia_msg);
            continue;
        }

        fprintf(stderr, "[ERRO] Falha inesperada esperando ACK/NACK\n");
        return -1;
    }

    fprintf(stderr,
            "[ERRO] Número máximo de tentativas atingido para seq=%u\n",
            mensagem->num_sequencia_msg);

    return -1;
}

// Envia mensagens grandes, separando-as em blocos
static int envia_buffer_protocolado(
    int soquete,
    uint8_t tipo_msg,
    const uint8_t *buffer,
    size_t tamanho_buffer)
{
    size_t offset = 0;

    if (buffer == NULL && tamanho_buffer > 0)
    {
        fprintf(stderr, "[ERRO] Buffer nulo com tamanho maior que zero\n");
        return -1;
    }

    while (offset < tamanho_buffer)
    {
        mensagem_t mensagem;
        size_t bytes_restantes = tamanho_buffer - offset;
        uint8_t tamanho_bloco;

        memset(&mensagem, 0, sizeof(mensagem));

        if (bytes_restantes > TAMANHO_MAX_DADOS)
        {
            tamanho_bloco = TAMANHO_MAX_DADOS;
        }
        else
        {
            tamanho_bloco = (uint8_t)bytes_restantes;
        }

        mensagem.tipo_msg = tipo_msg;
        mensagem.tamanho_dados = tamanho_bloco;

        memcpy(
            mensagem.dados,
            buffer + offset,
            tamanho_bloco);

        if (envia_pacote_com_reenvio(soquete, &mensagem) != 0)
        {
            fprintf(stderr,
                    "[ERRO] Falha ao enviar bloco no offset %zu\n",
                    offset);
            return -1;
        }

        offset += tamanho_bloco;
    }

    /*
     * Depois de enviar todos os blocos, envia um pacote especial
     * informando que a transmissão terminou.
     */
    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));

    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    if (envia_pacote_com_reenvio(soquete, &fim) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO\n");
        return -1;
    }

    printf("[DEBUG] Transmissão em blocos finalizada\n");

    return 0;
}

static int espera_ack_nack_com_timeout(int soquete, uint8_t sequencia_esperada)
{
    // Buffer que recebe o pacote de resposta
    uint8_t pacote_resposta[TAMANHO_MAX_PACOTE];
    mensagem_t resposta;

    while (1)
    {
        // Aguarda uma resposta respeitando o limite de tempo
        ssize_t recebido = espera_mensagem_timeout(
            soquete,
            pacote_resposta,
            sizeof(pacote_resposta),
            TIMEOUT_ACK_MS);

        // Se nao tiver resposta dentro do prazo, avisa o chamador
        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr,
                    "[DEBUG] Timeout esperando ACK/NACK seq=%u\n",
                    sequencia_esperada);
            return REDE_TIMEOUT;
        }

        // Interrupcoes por sinal nao encerram a espera
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_timeout");
            return -1;
        }

        // Ignora leituras vazias
        if (recebido == 0)
        {
            continue;
        }

        // Converte o pacote recebido em mensagem
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

        // ACK confirma o pacote enviado
        if (resposta.tipo_msg == MSG_ACK)
        {
            printf("[DEBUG] ACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_ACK;
        }

        // NACK pede reenvio do pacote
        if (resposta.tipo_msg == MSG_NACK)
        {
            printf("[DEBUG] NACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_NACK;
        }

        // Outros tipos sao ignorados enquanto o prazo nao acaba
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

    // Envia a mensagem em blocos, trata ACK, NACK e timeout.
    return envia_buffer_protocolado(
        soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem));
}
