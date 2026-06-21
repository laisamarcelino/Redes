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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

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

/* APAGAR - Entender isso aqui
 * Junta um fragmento recebido em um buffer dinamico.
 * A funcao aumenta a capacidade quando necessario e mantém o buffer terminado
 * em '\0' para permitir imprimir a visualizacao como texto.
 */
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

// Monta e envia uma mensagem de inicializacao para pedir ao servidor o mapa atual
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

/* APAGAR
 * Monta e envia uma mensagem de movimento do PacMan.
 * O tipo da mensagem ja chega convertido para MSG_MOV_CIMA, MSG_MOV_BAIXO,
 * MSG_MOV_ESQUERDA ou MSG_MOV_DIREITA.
 */
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

/* APAGAR
 * Recebe os fragmentos de visualizacao enviados pelo servidor, confirma cada
 * pacote com ACK e imprime o mapa remontado ao final da transmissao.
 */
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

// Abre o arquivo com o programa padrao do sistema sem bloquear o jogo.
static void abre_arquivo_externo(const char *caminho)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("xdg-open", "xdg-open", caminho, (char *)NULL);
        _exit(1);
    }
    // Processo pai continua sem esperar o filho
}

#define PASTA_RECEBIDOS "recebidos"

// Salva o buffer em recebidos/premio_NNN.ext e preenche caminho com o nome usado.
static int salva_em_recebidos(const uint8_t *buffer, size_t tamanho,
                              const char *ext, char *caminho, size_t tam_caminho)
{
    for (int i = 1; i <= 999; i++)
    {
        snprintf(caminho, tam_caminho, "%s/premio_%03d.%s", PASTA_RECEBIDOS, i, ext);

        FILE *teste = fopen(caminho, "rb");
        if (teste != NULL) { fclose(teste); continue; }

        FILE *f = fopen(caminho, "wb");
        if (f == NULL) { perror("fopen premio"); return -1; }
        if (tamanho > 0) fwrite(buffer, 1, tamanho, f);
        fclose(f);
        return 0;
    }

    fprintf(stderr, "[AVISO] Nao foi possivel salvar o arquivo de premio\n");
    return -1;
}

// Salva o arquivo recebido em recebidos/ e exibe ou abre conforme o tipo.
static void exibe_arquivo_recebido(uint8_t tipo, const uint8_t *buffer, size_t tamanho)
{
    const char *ext = (tipo == MSG_TXT) ? "txt" : (tipo == MSG_JPG) ? "jpg" : "mp4";
    char caminho[128];

    if (salva_em_recebidos(buffer, tamanho, ext, caminho, sizeof(caminho)) != 0)
        return;

    if (tipo == MSG_TXT)
    {
        printf("\n=== Premio recebido (texto) — salvo em %s ===\n", caminho);
        fwrite(buffer, 1, tamanho, stdout);
        printf("\n=============================================\n");
    }
    else
    {
        printf("\n=== Premio salvo em: %s — abrindo... ===\n", caminho);
        abre_arquivo_externo(caminho);
    }
}

/*
 * Recebe o arquivo enviado pelo servidor logo apos o mapa.
 * O servidor garante enviar sempre algo (arquivo ou FIM_TRANSMISSAO vazio),
 * portanto nao e necessario timeout — usa recebimento bloqueante direto.
 */
static void recebe_arquivo_se_disponivel(int soquete)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;
    uint8_t sequencia_esperada = 0;
    uint8_t *buffer = NULL;
    size_t tamanho = 0;
    size_t capacidade = 0;
    uint8_t tipo_atual = MSG_DADOS;

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(soquete, pacote, sizeof(pacote));

        if (recebido < 0)
        {
            if (errno == EINTR) continue;
            free(buffer);
            return;
        }

        if (recebido == 0) continue;

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            if ((size_t)recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                envia_ack_nack(soquete, MSG_NACK,
                               extrai_sequencia_pacote_bruto(pacote));
            }
            continue;
        }

        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada))
        {
            envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.num_sequencia_msg != sequencia_esperada)
        {
            envia_ack_nack(soquete, MSG_NACK, mensagem.num_sequencia_msg);
            continue;
        }

        // FIM_TRANSMISSAO: fim do arquivo (ou sinal de "sem arquivo")
        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
            if (buffer != NULL)
                exibe_arquivo_recebido(tipo_atual, buffer, tamanho);
            free(buffer);
            return;
        }

        // Tipo inesperado: nao e arquivo, desbloqueia sem exibir nada
        if (mensagem.tipo_msg != MSG_TXT &&
            mensagem.tipo_msg != MSG_JPG &&
            mensagem.tipo_msg != MSG_MP4)
        {
            envia_ack_nack(soquete, MSG_NACK, mensagem.num_sequencia_msg);
            free(buffer);
            return;
        }

        tipo_atual = mensagem.tipo_msg;

        if (tamanho + mensagem.tamanho_dados > capacidade)
        {
            size_t nova = (capacidade == 0) ? 256 : capacidade;
            while (tamanho + mensagem.tamanho_dados > nova) nova *= 2;

            uint8_t *nb = realloc(buffer, nova);
            if (nb == NULL)
            {
                fprintf(stderr, "[ERRO] Falha ao alocar buffer do arquivo\n");
                free(buffer);
                return;
            }

            buffer = nb;
            capacidade = nova;
        }

        memcpy(buffer + tamanho, mensagem.dados, mensagem.tamanho_dados);
        tamanho += mensagem.tamanho_dados;

        envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
        sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);
    }
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* 
 * Executa o modo cliente.
 * Decide se o argumento representa envio de arquivo, pedido de mapa,
 * movimento do PacMan ou mensagem comum, e usa o protocolo adequado.
 */
int executa_cliente(int soquete, const char *mensagem)
{
    uint8_t tipo_movimento;

    // Valida a mensagem recebida pela linha de comando
    if (mensagem == NULL || mensagem[0] == '\0')
    {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    /* APAGAR
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

    /* APAGAR
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

        int resultado = recebe_mapa_completo(soquete);
        recebe_arquivo_se_disponivel(soquete);
        return resultado;
    }

    // Envia a mensagem em blocos, trata ACK, NACK e timeout.
    return envia_buffer_protocolado(
        soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem),
        &proxima_sequencia_cliente);
}
