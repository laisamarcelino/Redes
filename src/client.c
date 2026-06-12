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

// Acrescenta um fragmento recebido ao buffer remontado.
static int acumula_fragmento(uint8_t **buffer, size_t *tamanho_atual,
                             size_t *capacidade, const uint8_t *dados,
                             size_t tamanho_dados)
{
    if (tamanho_dados == 0)
    {
        return 0;
    }

    if (*tamanho_atual + tamanho_dados + 1 > *capacidade)
    {
        size_t nova_capacidade = (*capacidade == 0) ? 256 : *capacidade;

        while (*tamanho_atual + tamanho_dados + 1 > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        uint8_t *novo_buffer = realloc(*buffer, nova_capacidade);
        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao realocar visualizacao recebida\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    memcpy(*buffer + *tamanho_atual, dados, tamanho_dados);
    *tamanho_atual += tamanho_dados;
    (*buffer)[*tamanho_atual] = '\0';

    return 0;
}

// Envia ao servidor um pedido simples para iniciar/consultar o mapa do jogo.
static int envia_pedido_mapa(int soquete)
{
    mensagem_t mensagem;

    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo_msg = MSG_INICIALIZACAO;
    mensagem.tamanho_dados = 0;

    return envia_pacote_com_reenvio(
        soquete,
        &mensagem,
        &proxima_sequencia_cliente);
}

// Recebe a visualizacao enviada pelo servidor e imprime o mapa completo.
static int recebe_mapa_completo(int soquete)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;
    uint8_t *visualizacao = NULL;
    size_t tamanho_visualizacao = 0;
    size_t capacidade_visualizacao = 0;
    uint8_t sequencia_esperada = 0;

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            free(visualizacao);
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido pelo cliente\n");

            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                envia_ack_nack(
                    soquete,
                    MSG_NACK,
                    extrai_sequencia_pacote_bruto(pacote));
            }

            continue;
        }

        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada))
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.num_sequencia_msg != sequencia_esperada)
        {
            envia_ack_nack(
                soquete,
                MSG_NACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.tipo_msg == MSG_VISUALIZACAO)
        {
            if (acumula_fragmento(
                    &visualizacao,
                    &tamanho_visualizacao,
                    &capacidade_visualizacao,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(visualizacao);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (visualizacao != NULL)
            {
                printf("%s", visualizacao);
            }

            free(visualizacao);
            return 0;
        }

        fprintf(stderr, "[ERRO] Tipo inesperado ao receber mapa: %u\n",
                mensagem.tipo_msg);
        envia_ack_nack(
            soquete,
            MSG_NACK,
            mensagem.num_sequencia_msg);
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

    /*
     * Pedido temporario de jogo:
     * envia MSG_INICIALIZACAO e aguarda o servidor responder com o mapa completo.
     */
    if (strcmp(mensagem, "mapa") == 0 || strcmp(mensagem, "iniciar") == 0)
    {
        if (envia_pedido_mapa(soquete) != 0)
        {
            return -1;
        }

        return recebe_mapa_completo(soquete);
    }

    // Envia a mensagem em blocos, trata ACK, NACK e timeout.
    return envia_buffer_protocolado(
        soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem),
        &proxima_sequencia_cliente);
}
