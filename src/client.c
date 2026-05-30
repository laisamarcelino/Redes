#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

static int envia_mensagem_protocolada(int soquete, const char *texto)
{
    mensagem_t mensagem;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    size_t tamanho_texto;

    // Nao envia mensagens vazias
    if (texto == NULL || texto[0] == '\0')
    {
        fprintf(stderr, "[ERRO] Texto nulo ou vazio\n");
        return -1;
    }

    // Calcula quantos bytes de dados serao enviados
    tamanho_texto = strlen(texto);

    /* DEBUG
     * Nesta primeira etapa, ainda nao vamos fragmentar.
     * Entao a mensagem precisa caber em uma unica mensagem do protocolo.
     */
    if (tamanho_texto > TAMANHO_MAX_DADOS)
    {
        fprintf(stderr,
                "[ERRO] Mensagem grande demais para um pacote. Maximo: %d bytes\n",
                TAMANHO_MAX_DADOS);
        return -1;
    }

    // Limpa a estrutura antes de preencher os campos
    memset(&mensagem, 0, sizeof(mensagem));

    // Preenche os campos do cabecalho logico
    mensagem.tipo_msg = MSG_DADOS;
    mensagem.num_sequencia_msg = 0;
    mensagem.tamanho_dados = (uint8_t)tamanho_texto;

    // Copia o texto para o campo de dados
    memcpy(mensagem.dados, texto, tamanho_texto);

    // Monta o pacote no formato enviado pela rede
    if (monta_pacote(&mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return -1;
    }

    printf("[DEBUG] Enviando pacote com %zu bytes\n", tamanho_pacote);

    // Envia somente o tamanho real do pacote; so envia bytes
    ssize_t resultado = envia_mensagem(soquete, pacote, tamanho_pacote);

    // Tenta novamente se o envio foi interrompido por sinal do SO
    if (resultado < 0)
    {
        if (errno == EINTR)
        {
            // Monta pacote do protocolo e controla confirmação, usa envia_mensagem
            return envia_mensagem_protocolada(soquete, texto);
        }

        perror("envia_mensagem");
        return -1;
    }

    // Verifica se todos os bytes foram enviados
    if ((size_t)resultado != tamanho_pacote)
    {
        fprintf(stderr,
                "[ERRO] Envio incompleto. Enviado: %zd, esperado: %zu\n",
                resultado,
                tamanho_pacote);
        return -1;
    }

    return 0;
}

static int espera_ack_nack(int soquete, uint8_t sequencia_esperada)
{
    // Buffer usado para receber a resposta do servidor
    uint8_t pacote_resposta[TAMANHO_MAX_PACOTE];
    mensagem_t resposta;

    while (1)
    {
        // Aguarda um pacote de controle vindo da rede
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote_resposta,
            sizeof(pacote_resposta));

        // Interrupcoes por sinal nao encerram a espera
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        // Ignora leituras sem conteudo
        if (recebido == 0)
        {
            continue;
        }

        // Converte o pacote recebido em mensagem do protocolo
        if (desmonta_pacote(
                pacote_resposta,
                (size_t)recebido,
                &resposta) != 0)
        {
            fprintf(stderr, "[ERRO] Cliente recebeu pacote invalido\n");
            return -1;
        }

        // Ignora respostas de outra sequencia
        if (resposta.num_sequencia_msg != sequencia_esperada)
        {
            fprintf(stderr,
                    "[DEBUG] Resposta ignorada. Esperado seq=%u, recebido seq=%u\n",
                    sequencia_esperada,
                    resposta.num_sequencia_msg);
            continue;
        }

        // ACK confirma o recebimento pelo servidor
        if (resposta.tipo_msg == MSG_ACK)
        {
            printf("[DEBUG] ACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_ACK;
        }

        // NACK indica que o servidor rejeitou o pacote
        if (resposta.tipo_msg == MSG_NACK)
        {
            printf("[DEBUG] NACK recebido para seq=%u\n", sequencia_esperada);
            return MSG_NACK;
        }

        // Outros tipos nao encerram a espera
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

    // Envia a mensagem usando o protocolo PacMan
    if (envia_mensagem_protocolada(soquete, mensagem) != 0)
    {
        return -1;
    }

    // Aguarda a resposta de controle do servidor
    int resposta = espera_ack_nack(
        soquete,
        0);

    // ACK encerra o envio
    if (resposta == MSG_ACK)
    {
        printf("[DEBUG] Envio confirmado\n");
        return 0;
    }
    // NACK indica que a mensagem deveria ser reenviada
    else if (resposta == MSG_NACK)
    {
        fprintf(stderr, "[ERRO] Servidor pediu reenvio\n");
        return -1;
    }
    // Qualquer outro retorno indica erro de espera
    else
    {
        fprintf(stderr, "[ERRO] Falha ao esperar resposta\n");
        return -1;
    }
}
