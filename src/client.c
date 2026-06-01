#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

static uint8_t proxima_sequencia_cliente = 0;

// Envia mensagens grandes, separando-as em blocos
static int envia_buffer_protocolado(
    int soquete,
    uint8_t tipo_msg,
    const uint8_t *buffer,
    size_t tamanho_buffer,
    uint8_t *proxima_sequencia)
{
    // Offset indica o inicio do proximo bloco
    size_t offset = 0;

    if (buffer == NULL && tamanho_buffer > 0)
    {
        fprintf(stderr, "[ERRO] Buffer nulo com tamanho maior que zero\n");
        return -1;
    }

    // Envia todos os blocos de dados
    while (offset < tamanho_buffer)
    {
        // Mensagem temporaria para o bloco atual
        mensagem_t mensagem;
        size_t bytes_restantes = tamanho_buffer - offset;
        uint8_t tamanho_bloco;

        // Limpa a mensagem antes de preencher
        memset(&mensagem, 0, sizeof(mensagem));

        // Limita cada bloco ao tamanho maximo do protocolo
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

        // Copia o trecho atual do buffer para a mensagem
        memcpy(
            mensagem.dados,
            buffer + offset,
            tamanho_bloco);

        // Envia o bloco com reenvio em caso de falha temporaria
        if (envia_pacote_com_reenvio(
                soquete,
                &mensagem,
                proxima_sequencia) != 0)
        {
            fprintf(stderr,
                    "[ERRO] Falha ao enviar bloco no offset %zu\n",
                    offset);
            return -1;
        }

        // Avanca para o proximo trecho do buffer
        offset += tamanho_bloco;
    }

    /* Depois de enviar todos os blocos, envia um pacote especial
     * informando que a transmissão terminou
     */
    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));

    // Marca o pacote como fim de transmissao
    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    // Envia o pacote final para o servidor imprimir a mensagem completa
    if (envia_pacote_com_reenvio(
            soquete,
            &fim,
            proxima_sequencia) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO\n");
        return -1;
    }

    printf("[DEBUG] Transmissão em blocos finalizada\n");

    return 0;
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

    /*
     * Teste temporario:
     * se o argumento comecar com "arquivo:", envia o arquivo informado.
     *
     * Exemplo:
     *   sudo ./pacman -c "arquivo:premios/1.txt" -l
     */
    if (strncmp(mensagem, "arquivo:", 8) == 0)
    {
        const char *caminho = mensagem + 8;
        uint8_t tipo = tipo_arquivo_por_caminho(caminho);

        if (tipo == MSG_ERRO)
        {
            fprintf(stderr, "[ERRO] Extensao de arquivo nao suportada: %s\n", caminho);
            return -1;
        }

        return envia_arquivo_protocolado(
            soquete,
            caminho,
            tipo,
            &proxima_sequencia_cliente);
    }

    // Envia a mensagem em blocos, trata ACK, NACK e timeout.
    return envia_buffer_protocolado(
        soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem),
        &proxima_sequencia_cliente);
}
