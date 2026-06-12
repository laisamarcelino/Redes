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

// Converte texto digitado pelo usuario para o tipo de movimento do protocolo.
static uint8_t tipo_movimento_por_texto(const char *mensagem)
{
    if (mensagem == NULL)
    {
        return MSG_ERRO;
    }

    if (strcmp(mensagem, "cima") == 0 || strcmp(mensagem, "w") == 0)
        return MSG_MOV_CIMA;

    if (strcmp(mensagem, "baixo") == 0 || strcmp(mensagem, "s") == 0)
        return MSG_MOV_BAIXO;

    if (strcmp(mensagem, "esquerda") == 0 || strcmp(mensagem, "a") == 0)
        return MSG_MOV_ESQUERDA;

    if (strcmp(mensagem, "direita") == 0 || strcmp(mensagem, "d") == 0)
        return MSG_MOV_DIREITA;

    return MSG_ERRO;
}

// APAGAR - Entender isso aqui
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

// Envia uma jogada de movimento do PacMan ao servidor.
static int envia_movimento_pacman(int soquete, uint8_t tipo_movimento)
{
    mensagem_t mensagem;

    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo_msg = tipo_movimento;
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
    uint8_t tipo_movimento;

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

    tipo_movimento = tipo_movimento_por_texto(mensagem);
    if (tipo_movimento != MSG_ERRO)
    {
        if (envia_movimento_pacman(soquete, tipo_movimento) != 0)
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
