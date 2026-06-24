#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"
#include "game.h"
#include "log.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/
#define CAMINHO_MAPA_PADRAO "maps/padrao_ufpr.csv"

/* Retorna verdadeiro se o tipo da mensagem representa um arquivo (TXT, JPG ou MP4) */
static int tipo_arquivo(uint8_t tipo_msg)
{
    return tipo_msg == MSG_TXT || tipo_msg == MSG_JPG || tipo_msg == MSG_MP4;
}

/* Retorna verdadeiro se o tipo da mensagem é um comando de movimento do Pac-Man */
static int tipo_movimento(uint8_t tipo_msg)
{
    return tipo_msg == MSG_MOV_CIMA ||
           tipo_msg == MSG_MOV_BAIXO ||
           tipo_msg == MSG_MOV_ESQUERDA ||
           tipo_msg == MSG_MOV_DIREITA;
}

/* Preenche os deslocamentos de linha e coluna correspondentes ao comando de movimento recebido */
static int deslocamento_movimento(uint8_t tipo_msg, int *deslocamento_x, int *deslocamento_y)
{
    if (deslocamento_x == NULL || deslocamento_y == NULL)
    {
        return -1;
    }

    *deslocamento_x = 0;
    *deslocamento_y = 0;

    switch (tipo_msg)
    {
    case MSG_MOV_CIMA:
        *deslocamento_x = -1;
        return 0;

    case MSG_MOV_BAIXO:
        *deslocamento_x = 1;
        return 0;

    case MSG_MOV_ESQUERDA:
        *deslocamento_y = -1;
        return 0;

    case MSG_MOV_DIREITA:
        *deslocamento_y = 1;
        return 0;

    default:
        return -1;
    }
}

/* Retorna a string de extensão correspondente ao tipo de arquivo da mensagem */
static const char *extensao_saida_arquivo(uint8_t tipo_msg)
{
    if (tipo_msg == MSG_TXT)
    {
        return "txt";
    }

    if (tipo_msg == MSG_JPG)
    {
        return "jpg";
    }

    if (tipo_msg == MSG_MP4)
    {
        return "mp4";
    }

    return NULL;
}

/* Retorna verdadeiro se um arquivo com o caminho informado já existe no disco */
static int arquivo_existe(const char *caminho)
{
    struct stat st;

    if (stat(caminho, &st) == 0)
    {
        return 1;
    }

    if (errno != ENOENT)
    {
        perror("stat arquivo recebido");
        return 1;
    }

    return 0;
}

/* Gera um nome de arquivo numerado sequencialmente para guardar o conteúdo recebido */
static int monta_caminho_saida_arquivo(uint8_t tipo_msg, char *caminho_saida,
                                       size_t tamanho_caminho_saida)
{
    const char *extensao = extensao_saida_arquivo(tipo_msg);

    if (extensao == NULL || caminho_saida == NULL || tamanho_caminho_saida == 0)
    {
        return -1;
    }

    // Arquivos recebidos nunca sobrescrevem entregas anteriores.
    for (int indice = 1; indice <= 999; indice++)
    {
        int escritos = snprintf(
            caminho_saida,
            tamanho_caminho_saida,
            "recebido_%03d.%s",
            indice,
            extensao);

        if (escritos < 0 || (size_t)escritos >= tamanho_caminho_saida)
        {
            return -1;
        }

        if (!arquivo_existe(caminho_saida))
        {
            return 0;
        }
    }

    fprintf(stderr, "[ERRO] Limite de nomes recebidos_NNN.%s atingido\n", extensao);
    return -1;
}

/* Exibe no terminal o conteúdo da mensagem recebida, escapando bytes não imprimíveis */
static void imprime_mensagem_protocolada(const mensagem_t *mensagem)
{
    if (mensagem == NULL)
    {
        return;
    }

    printf("Mensagem recebida: ");
    for (uint8_t i = 0; i < mensagem->tamanho_dados; i++)
    {
        unsigned char c = mensagem->dados[i];

        if (c >= 32 && c <= 126)
        {
            putchar(c);
        }
        else
        {
            printf("\\x%02X", c);
        }
    }
    putchar('\n');
    fflush(stdout);
}

/* Acrescenta um bloco de dados ao buffer de remontagem, expandindo-o se necessário */
static int remonta_mensagem(uint8_t **buffer, size_t *tamanho_atual,
                            size_t *capacidade, const uint8_t *dados,
                            size_t tamanho_dados)
{
    if (tamanho_dados == 0)
    {
        return 0;
    }

    // A remontagem aceita arquivos maiores que o payload máximo de 31 bytes.
    if (*tamanho_atual + tamanho_dados > *capacidade)
    {
        size_t nova_capacidade = (*capacidade == 0) ? 128 : *capacidade;

        while (*tamanho_atual + tamanho_dados > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        uint8_t *novo_buffer = realloc(*buffer, nova_capacidade);

        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao realocar buffer de recebimento\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    memcpy(
        *buffer + *tamanho_atual,
        dados,
        tamanho_dados);

    *tamanho_atual += tamanho_dados;

    return 0;
}

/* Exibe no terminal o conteúdo completo remontado de uma mensagem de texto */
static void imprime_mensagem_completa(const uint8_t *buffer, size_t tamanho)
{
    printf("Mensagem recebida: ");

    if (buffer != NULL && tamanho > 0)
    {
        fwrite(buffer, 1, tamanho, stdout);
    }

    printf("\n");
    fflush(stdout);
}

/* Grava o conteúdo remontado em um novo arquivo no disco com o nome gerado sequencialmente */
static int salva_arquivo_completo(uint8_t tipo_msg, const uint8_t *buffer, size_t tamanho)
{
    char caminho_saida[64];

    if (monta_caminho_saida_arquivo(
            tipo_msg,
            caminho_saida,
            sizeof(caminho_saida)) != 0)
    {
        fprintf(stderr, "[ERRO] Tipo de arquivo sem caminho de saida: %u\n", tipo_msg);
        return -1;
    }

    FILE *arquivo = fopen(caminho_saida, "wb");
    if (arquivo == NULL)
    {
        perror("fopen arquivo recebido");
        return -1;
    }

    // Binários recebidos são gravados sem conversão para preservar imagem e vídeo.
    if (tamanho > 0 && fwrite(buffer, 1, tamanho, arquivo) != tamanho)
    {
        perror("fwrite arquivo recebido");
        fclose(arquivo);
        return -1;
    }

    if (fclose(arquivo) != 0)
    {
        perror("fclose arquivo recebido");
        return -1;
    }

    return 0;
}

/* Gera a visualização do estado atual do jogo e a envia ao cliente fragmentada em blocos */
static int envia_mapa_completo(int *p_soquete, const jogo_t *jogo)
{
    char visualizacao[JOGO_VISUALIZACAO_MAX];
    size_t tamanho_visualizacao = 0;
    uint8_t proxima_sequencia = 0;

    if (gera_visualizacao(
            jogo,
            visualizacao,
            sizeof(visualizacao),
            &tamanho_visualizacao) != 0)
    {
        return -1;
    }

    return envia_buffer_protocolado(
        p_soquete,
        MSG_VISUALIZACAO,
        (const uint8_t *)visualizacao,
        tamanho_visualizacao,
        &proxima_sequencia);
}

/* Mapeia o símbolo de uma pastilha ao caminho e tipo do arquivo de prêmio correspondente */
static int caminho_arquivo_pastilha(char simbolo, const char **caminho, uint8_t *tipo)
{
    static const struct
    {
        char simbolo;
        const char *caminho;
        uint8_t tipo;
    } premios[] = {
        {LAB_PASTILHA_TXT_1, "pastilhas/1.txt", MSG_TXT},
        {LAB_PASTILHA_TXT_2, "pastilhas/2.txt", MSG_TXT},
        {LAB_PASTILHA_JPG_3, "pastilhas/3.jpg", MSG_JPG},
        {LAB_PASTILHA_JPG_4, "pastilhas/4.jpg", MSG_JPG},
        {LAB_PASTILHA_MP4_5, "pastilhas/5.mp4", MSG_MP4},
        {LAB_PASTILHA_MP4_6, "pastilhas/6.mp4", MSG_MP4},
    };

    for (size_t i = 0; i < sizeof(premios) / sizeof(premios[0]); i++)
    {
        if (premios[i].simbolo == simbolo)
        {
            *caminho = premios[i].caminho;
            *tipo = premios[i].tipo;
            return 0;
        }
    }

    return -1;
}

/* Envia ao cliente o arquivo de prêmio correspondente à última pastilha coletada */
static int envia_premio(int *p_soquete, jogo_t *jogo)
{
    const char *caminho = NULL;
    uint8_t tipo = MSG_DADOS;
    uint8_t seq = 0;

    if (jogo->ultima_pastilha_coletada == 0)
        return 0;

    if (caminho_arquivo_pastilha(
            (char)jogo->ultima_pastilha_coletada,
            &caminho,
            &tipo) != 0)
    {
        jogo->ultima_pastilha_coletada = 0;
        return 0;
    }

    jogo->ultima_pastilha_coletada = 0;
    return envia_arquivo_protocolado(p_soquete, caminho, tipo, &seq);
}

/* Envia ao cliente o arquivo de aviso quando o Pac-Man é pego por um fantasma */
static int envia_arquivo_colisao(int *p_soquete)
{
    uint8_t seq = 0;
    return envia_arquivo_protocolado(
        p_soquete,
        "pastilhas/colisao.txt",
        MSG_TXT,
        &seq);
}

/*
 * Envia a resposta completa apos o mapa:
 *   1. Arquivo de premio / colisao  -ou-  FIM_TRANSMISSAO vazio (sem arquivo)
 *   2. MSG_FIM_JOGO com status: 0=continua 1=vitoria 2=derrota
 *
 * O cliente sempre espera exatamente essa sequencia, sem timeout.
 */
static int envia_resposta_completa(int *p_soquete, jogo_t *jogo)
{
    // O cliente sempre recebe primeiro um arquivo real ou um FIM_TRANSMISSAO vazio.
    if (jogo->terminou && !jogo->venceu)
    {
        jogo->ultima_pastilha_coletada = 0;
        if (envia_arquivo_colisao(p_soquete) != 0)
        {
            return -1;
        }
    }
    else if (jogo->ultima_pastilha_coletada != 0)
    {
        if (envia_premio(p_soquete, jogo) != 0)
        {
            return -1;
        }
    }
    else
    {
        uint8_t seq = 0;
        if (envia_buffer_protocolado(p_soquete, MSG_DADOS, NULL, 0, &seq) != 0)
        {
            return -1;
        }
    }

    // O status vem depois para o cliente saber se deve continuar o loop interativo.
    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));
    fim.tipo_msg      = MSG_FIM_JOGO;
    fim.tamanho_dados = 1;
    fim.dados[0]      = (uint8_t)(jogo->terminou
                            ? (jogo->venceu ? 1 : 2)
                            : 0);
    uint8_t seq = 0;
    return envia_pacote_com_reenvio(p_soquete, &fim, &seq);
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* Loop principal do servidor: recebe pacotes, atualiza o estado do jogo e responde ao cliente */
int executa_servidor(int soquete, const char *caminho_mapa)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;
    uint8_t *buffer_recebido = NULL;
    size_t tamanho_recebido = 0;
    size_t capacidade_recebido = 0;
    uint8_t sequencia_esperada = 0;
    uint8_t tipo_transmissao_atual = MSG_DADOS;
    jogo_t jogo;

    const char *mapa = (caminho_mapa != NULL) ? caminho_mapa : CAMINHO_MAPA_PADRAO;
    printf("Carregando mapa: %s\n", mapa);

    log_define_contexto("SRV");
    log_evento("mapa: %s", mapa);

    if (carrega_mapa_csv(&jogo, mapa) != 0)
    {
        return -1;
    }

    if (posiciona_entidades_no_mapa(&jogo) != 0)
    {
        return -1;
    }

    printf("Servidor aguardando pacotes do protocolo PacMan...\n");

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(
            &soquete,
            pacote,
            sizeof(pacote));

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido\n");

            // Quando possível, o NACK usa a sequência do pacote corrompido para pedir reenvio.
            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                uint8_t sequencia_erro = extrai_sequencia_pacote_bruto(pacote);

                envia_ack_nack(
                    soquete,
                    MSG_NACK,
                    sequencia_erro);
            }

            continue;
        }

        log_mensagem("CLI", &mensagem);

        // Duplicatas recebem ACK, mas não são processadas de novo.
        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada))
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        // Fora de ordem é tratado com NACK para manter o protocolo stop-and-wait.
        if (mensagem.num_sequencia_msg != sequencia_esperada)
        {
            fprintf(stderr,
                    "[ERRO] Sequencia inesperada. Recebido: %u, esperado: %u\n",
                    mensagem.num_sequencia_msg,
                    sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_NACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.tipo_msg == MSG_DADOS)
        {
            if (remonta_mensagem(
                    &buffer_recebido,
                    &tamanho_recebido,
                    &capacidade_recebido,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        if (tipo_arquivo(mensagem.tipo_msg))
        {
            tipo_transmissao_atual = mensagem.tipo_msg;

            if (remonta_mensagem(
                    &buffer_recebido,
                    &tamanho_recebido,
                    &capacidade_recebido,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        if (mensagem.tipo_msg == MSG_INICIALIZACAO)
        {
            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (envia_mapa_completo(&soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            if (envia_resposta_completa(&soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            // Cada resposta completa reinicia a sequência esperada do cliente.
            sequencia_esperada = 0;

            continue;
        }

        if (tipo_movimento(mensagem.tipo_msg))
        {
            int deslocamento_x;
            int deslocamento_y;

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (deslocamento_movimento(
                    mensagem.tipo_msg,
                    &deslocamento_x,
                    &deslocamento_y) != 0 ||
                movimenta_pacman(&jogo, deslocamento_x, deslocamento_y) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            log_evento("pacman (%d,%d) rodada=%d raio=%d",
                       jogo.pacman.posicao_x, jogo.pacman.posicao_y,
                       jogo.rodada, jogo.raio_visao);

            if (jogo.ultima_pastilha_coletada != 0)
                log_evento("pastilha '%c' coletada (%d/%d)",
                           (char)jogo.ultima_pastilha_coletada,
                           jogo.pastilhas_coletadas, TOTAL_PASTILHAS);

            movimenta_fantasmas(&jogo);

            if (jogo.terminou)
                log_evento("jogo encerrado: %s",
                           jogo.venceu ? "VITORIA" : "DERROTA por colisao");

            if (envia_mapa_completo(&soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            if (envia_resposta_completa(&soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            sequencia_esperada = 0;

            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (tipo_arquivo(tipo_transmissao_atual))
            {
                if (salva_arquivo_completo(
                        tipo_transmissao_atual,
                        buffer_recebido,
                        tamanho_recebido) != 0)
                {
                    free(buffer_recebido);
                    return -1;
                }
            }
            else
            {
                imprime_mensagem_completa(buffer_recebido, tamanho_recebido);
            }

            free(buffer_recebido);
            buffer_recebido = NULL;
            tamanho_recebido = 0;
            capacidade_recebido = 0;
            tipo_transmissao_atual = MSG_DADOS;
            // O fim da transmissão fecha a janela stop-and-wait atual.
            sequencia_esperada = 0;

            continue;
        }

        imprime_mensagem_protocolada(&mensagem);

        sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);
    }
}
