#define _POSIX_C_SOURCE 200809L

#include "game.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SUCESSO 0
#define ERRO -1

#define DIRECAO_CIMA 0
#define DIRECAO_DIREITA 1
#define DIRECAO_BAIXO 2
#define DIRECAO_ESQUERDA 3

/* ===================================================================
                         FUNCOES AUXILIARES
======================================================================*/

static void inicializa_personagem(personagem_t *personagem, char simbolo)
{
    if (personagem == NULL)
    {
        return;
    }

    personagem->posicao_x = 0;
    personagem->posicao_y = 0;
    personagem->ativo = 0;
    personagem->simbolo = simbolo;
    personagem->direcao = 0;
}

static void define_personagem(personagem_t *personagem, int x, int y, char simbolo)
{
    if (personagem == NULL)
    {
        return;
    }

    personagem->posicao_x = x;
    personagem->posicao_y = y;
    personagem->ativo = 1;
    personagem->simbolo = simbolo;
    personagem->direcao = 0;
}

/* Confere se um caractere pertence ao conjunto de simbolos do labirinto. */
static int eh_representacao_valida(char simbolo)
{
    switch (simbolo)
    {
    case LAB_PACMAN:
    case LAB_PAREDE:
    case LAB_VAZIO:

    case LAB_FANTASMA_VERMELHO:
    case LAB_FANTASMA_AZUL:
    case LAB_FANTASMA_VERDE:
    case LAB_FANTASMA_AMARELO:

    case LAB_PASTILHA_TXT_1:
    case LAB_PASTILHA_TXT_2:
    case LAB_PASTILHA_JPG_3:
    case LAB_PASTILHA_JPG_4:
    case LAB_PASTILHA_MP4_5:
    case LAB_PASTILHA_MP4_6:
        return SUCESSO;

    default:
        return ERRO;
    }
}

static int posicao_valida_mapa(int x, int y)
{
    return x >= 0 && x < LINHAS && y >= 0 && y < COLUNAS;
}

/* Garante que srand seja chamado uma unica vez durante a execucao. */
static void inicializa_gerador_aleatorio(void)
{
    static int inicializado = 0;

    if (!inicializado)
    {
        srand((unsigned int)time(NULL));
        inicializado = 1;
    }
}

static char primeiro_caractere_util(const char *texto)
{
    if (texto == NULL)
    {
        return '\0';
    }

    while (*texto != '\0' && isspace((unsigned char)*texto))
    {
        texto++;
    }

    return *texto;
}

static int personagem_ocupa_posicao(const personagem_t *personagem, int x, int y)
{
    return personagem != NULL &&
           personagem->ativo &&
           personagem->posicao_x == x &&
           personagem->posicao_y == y;
}

static int posicao_ocupada_por_personagem(const jogo_t *jogo, int x, int y)
{
    return personagem_ocupa_posicao(&jogo->pacman, x, y) ||
           personagem_ocupa_posicao(&jogo->fantasma_vermelho, x, y) ||
           personagem_ocupa_posicao(&jogo->fantasma_azul, x, y) ||
           personagem_ocupa_posicao(&jogo->fantasma_verde, x, y) ||
           personagem_ocupa_posicao(&jogo->fantasma_amarelo, x, y);
}

static int posicao_disponivel(const jogo_t *jogo, int x, int y)
{
    /* Personagens e pastilhas nao podem ser sorteados sobre paredes,
     * pastilhas existentes ou outros personagens.
     */
    return jogo != NULL &&
           posicao_valida_mapa(x, y) &&
           jogo->mapa[x][y] == LAB_VAZIO &&
           !posicao_ocupada_por_personagem(jogo, x, y);
}

static int linha_tem_conteudo(const char *linha)
{
    if (linha == NULL)
    {
        return 0;
    }

    while (*linha != '\0')
    {
        if (!isspace((unsigned char)*linha))
        {
            return 1;
        }

        linha++;
    }

    return 0;
}

static int registra_personagem_csv(jogo_t *jogo, char simbolo, int x, int y)
{
    personagem_t *personagem = NULL;

    switch (simbolo)
    {
    case LAB_PACMAN:
        personagem = &jogo->pacman;
        break;
    case LAB_FANTASMA_VERMELHO:
        personagem = &jogo->fantasma_vermelho;
        break;
    case LAB_FANTASMA_AZUL:
        personagem = &jogo->fantasma_azul;
        break;
    case LAB_FANTASMA_VERDE:
        personagem = &jogo->fantasma_verde;
        break;
    case LAB_FANTASMA_AMARELO:
        personagem = &jogo->fantasma_amarelo;
        break;
    default:
        return ERRO;
    }

    if (personagem->ativo)
    {
        fprintf(stderr,
                "[ERRO] Personagem '%c' aparece mais de uma vez no CSV.\n",
                simbolo);
        return ERRO;
    }

    define_personagem(personagem, x, y, simbolo);
    return SUCESSO;
}

/* Sorteia uma posicao livre e ativa o personagem nela. */
static int posiciona_personagem_aleatorio(jogo_t *jogo, personagem_t *personagem, char simbolo)
{
    int x;
    int y;

    if (sorteia_posicao(jogo, &x, &y) != SUCESSO)
    {
        return ERRO;
    }

    define_personagem(personagem, x, y, simbolo);
    return SUCESSO;
}

static int posiciona_pastilha_aleatoria(jogo_t *jogo, char simbolo)
{
    int x;
    int y;

    if (sorteia_posicao(jogo, &x, &y) != SUCESSO)
    {
        return ERRO;
    }

    jogo->mapa[x][y] = simbolo;
    return SUCESSO;
}

/* ===================================================================
                         FUNCOES PRINCIPAIS
======================================================================*/

void inicializa_jogo(jogo_t *jogo)
{

    if (jogo == NULL)
        return;

    /* O jogo comeca com mapa limpo; depois o CSV ou o mapa padrao
     * preenche paredes, pastilhas e personagens.
     */
    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
            jogo->mapa[x][y] = LAB_VAZIO;
        }
    }

    inicializa_personagem(&jogo->pacman, LAB_PACMAN);

    inicializa_personagem(&jogo->fantasma_vermelho, LAB_FANTASMA_VERMELHO);
    inicializa_personagem(&jogo->fantasma_azul, LAB_FANTASMA_AZUL);
    inicializa_personagem(&jogo->fantasma_verde, LAB_FANTASMA_VERDE);
    inicializa_personagem(&jogo->fantasma_amarelo, LAB_FANTASMA_AMARELO);

    jogo->rodada = 0;
    jogo->raio_visao = RAIO_INICIAL;
    jogo->pastilhas_coletadas = 0;
    jogo->ultima_pastilha_coletada = 0;

    jogo->terminou = 0;
    jogo->venceu = 0;
}

int carrega_mapa_csv(jogo_t *jogo, const char *caminho_csv)
{
    FILE *arquivo;
    char linha_csv[512];

    if (jogo == NULL || caminho_csv == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em carrega_mapa_csv.\n");
        return ERRO;
    }

    arquivo = fopen(caminho_csv, "r");

    if (arquivo == NULL)
    {
        perror("[ERRO] Falha ao abrir arquivo CSV do mapa");
        return ERRO;
    }

    inicializa_jogo(jogo);

    for (int x = 0; x < LINHAS; x++)
    {
        char *token;

        if (fgets(linha_csv, sizeof(linha_csv), arquivo) == NULL)
        {
            fprintf(stderr,
                    "[ERRO] O mapa possui menos de %d linhas. Parou na linha %d.\n",
                    LINHAS,
                    x);
            fclose(arquivo);
            return ERRO;
        }

        token = strtok(linha_csv, ";\r\n");

        for (int y = 0; y < COLUNAS; y++)
        {
            char simbolo;

            if (token == NULL)
            {
                fprintf(stderr,
                        "[ERRO] Linha %d possui menos de %d colunas.\n",
                        x,
                        COLUNAS);
                fclose(arquivo);
                return ERRO;
            }

            simbolo = primeiro_caractere_util(token);

            if (eh_representacao_valida(simbolo) != SUCESSO)
            {
                fprintf(stderr,
                        "[ERRO] Simbolo invalido '%c' na posicao x=%d, y=%d.\n",
                        simbolo,
                        x,
                        y);
                fclose(arquivo);
                return ERRO;
            }

            /* Personagens sao guardados fora do mapa base. A celula
             * correspondente fica vazia para permitir movimentacao.
             */
            if (simbolo == LAB_PACMAN ||
                simbolo == LAB_FANTASMA_VERMELHO ||
                simbolo == LAB_FANTASMA_AZUL ||
                simbolo == LAB_FANTASMA_VERDE ||
                simbolo == LAB_FANTASMA_AMARELO)
            {
                if (registra_personagem_csv(jogo, simbolo, x, y) != SUCESSO)
                {
                    fclose(arquivo);
                    return ERRO;
                }

                jogo->mapa[x][y] = LAB_VAZIO;
            }
            else
            {
                jogo->mapa[x][y] = simbolo;
            }

            token = strtok(NULL, ";\r\n");
        }

        if (token != NULL)
        {
            fprintf(stderr,
                    "[ERRO] Linha %d possui mais de %d colunas.\n",
                    x,
                    COLUNAS);
            fclose(arquivo);
            return ERRO;
        }
    }

    while (fgets(linha_csv, sizeof(linha_csv), arquivo) != NULL)
    {
        if (linha_tem_conteudo(linha_csv))
        {
            fprintf(stderr,
                    "[ERRO] O mapa possui mais de %d linhas.\n",
                    LINHAS);
            fclose(arquivo);
            return ERRO;
        }
    }

    fclose(arquivo);

    return SUCESSO;
}

int sorteia_posicao(const jogo_t *jogo, int *x, int *y)
{
    int quantidade_disponivel = 0;
    int escolhido_x = 0;
    int escolhido_y = 0;

    if (jogo == NULL || x == NULL || y == NULL)
    {
        return ERRO;
    }

    inicializa_gerador_aleatorio();

    /* Reservoir sampling: escolhe uniformemente entre todas as posicoes
     * disponiveis sem precisar criar uma lista auxiliar.
     */
    for (int linha = 0; linha < LINHAS; linha++)
    {
        for (int coluna = 0; coluna < COLUNAS; coluna++)
        {
            if (!posicao_disponivel(jogo, linha, coluna))
            {
                continue;
            }

            quantidade_disponivel++;

            if (rand() % quantidade_disponivel == 0)
            {
                escolhido_x = linha;
                escolhido_y = coluna;
            }
        }
    }

    if (quantidade_disponivel == 0)
    {
        return ERRO;
    }

    *x = escolhido_x;
    *y = escolhido_y;
    return SUCESSO;
}

void inicializa_mapa_padrao(jogo_t *jogo)
{
    const char pastilhas[TOTAL_PASTILHAS] = {
        LAB_PASTILHA_TXT_1,
        LAB_PASTILHA_TXT_2,
        LAB_PASTILHA_JPG_3,
        LAB_PASTILHA_JPG_4,
        LAB_PASTILHA_MP4_5,
        LAB_PASTILHA_MP4_6};

    if (jogo == NULL)
    {
        return;
    }

    inicializa_jogo(jogo);

    /* Borda externa do labirinto. */
    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
            if (x == 0 || x == LINHAS - 1 || y == 0 || y == COLUNAS - 1)
            {
                jogo->mapa[x][y] = LAB_PAREDE;
            }
        }
    }

    /* Paredes internas desenham "UFPR" no mapa padrao. */
    for (int y = 4; y <= 9; y++)
    {
        jogo->mapa[6][y] = LAB_PAREDE;
        jogo->mapa[16][y] = LAB_PAREDE;
    }
    for (int x = 6; x <= 16; x++)
    {
        jogo->mapa[x][4] = LAB_PAREDE;
        jogo->mapa[x][9] = LAB_PAREDE;
    }
    jogo->mapa[16][5] = LAB_VAZIO;
    jogo->mapa[16][6] = LAB_VAZIO;
    jogo->mapa[16][7] = LAB_VAZIO;
    jogo->mapa[16][8] = LAB_VAZIO;

    for (int x = 6; x <= 16; x++)
    {
        jogo->mapa[x][13] = LAB_PAREDE;
        jogo->mapa[x][19] = LAB_PAREDE;
    }
    for (int y = 13; y <= 19; y++)
    {
        jogo->mapa[16][y] = LAB_PAREDE;
    }

    for (int x = 6; x <= 16; x++)
    {
        jogo->mapa[x][23] = LAB_PAREDE;
    }
    for (int y = 23; y <= 29; y++)
    {
        jogo->mapa[6][y] = LAB_PAREDE;
        jogo->mapa[11][y] = LAB_PAREDE;
    }
    for (int x = 6; x <= 11; x++)
    {
        jogo->mapa[x][29] = LAB_PAREDE;
    }

    for (int x = 6; x <= 16; x++)
    {
        jogo->mapa[x][33] = LAB_PAREDE;
    }
    for (int y = 33; y <= 37; y++)
    {
        jogo->mapa[6][y] = LAB_PAREDE;
        jogo->mapa[11][y] = LAB_PAREDE;
    }
    for (int x = 6; x <= 11; x++)
    {
        jogo->mapa[x][37] = LAB_PAREDE;
    }

    /* Personagens e pastilhas entram depois das paredes para evitar
     * sorteio em celulas bloqueadas.
     */
    if (posiciona_personagem_aleatorio(jogo, &jogo->pacman, LAB_PACMAN) != SUCESSO ||
        posiciona_personagem_aleatorio(jogo, &jogo->fantasma_vermelho, LAB_FANTASMA_VERMELHO) != SUCESSO ||
        posiciona_personagem_aleatorio(jogo, &jogo->fantasma_azul, LAB_FANTASMA_AZUL) != SUCESSO ||
        posiciona_personagem_aleatorio(jogo, &jogo->fantasma_verde, LAB_FANTASMA_VERDE) != SUCESSO ||
        posiciona_personagem_aleatorio(jogo, &jogo->fantasma_amarelo, LAB_FANTASMA_AMARELO) != SUCESSO)
    {
        return;
    }

    for (int i = 0; i < TOTAL_PASTILHAS; i++)
    {
        if (posiciona_pastilha_aleatoria(jogo, pastilhas[i]) != SUCESSO)
        {
            return;
        }
    }
}
