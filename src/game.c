#define _POSIX_C_SOURCE 200809L

#include "game.h"

#include <ctype.h>
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

#define NAO 0
#define SIM 1

/* ===================================================================
                         FUNCOES AUXILIARES
======================================================================*/

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

// Inicializa a struct do personagem
static void inicializa_personagem(personagem_t *personagem, char simbolo)
{
    if (personagem == NULL)
        return;

    personagem->posicao_x = -1;
    personagem->posicao_y = -1;
    personagem->ativo = 0;
    personagem->simbolo = simbolo;
    personagem->direcao = DIRECAO_CIMA;
}

// Verifica se uma posição do mapa é valida
static int posicao_valida(int x, int y)
{
    return x >= 0 && x < LINHAS && y >= 0 && y < COLUNAS;
}

static int personagem_ocupa_posicao(const personagem_t *personagem, int x, int y)
{
    if (personagem == NULL)
        return 0;

    if (!personagem->ativo)
        return 0;

    return personagem->posicao_x == x && personagem->posicao_y == y;
}

// Retorna se uma posição esta ocupada ou não
static int posicao_livre(const jogo_t *jogo, int x, int y)
{
    if (jogo == NULL)
        return 0;

    if (!posicao_valida(x, y))
        return 0;

    if (jogo->mapa[x][y] != LAB_VAZIO)
        return 0;

    if (personagem_ocupa_posicao(&jogo->pacman, x, y))
        return 0;

    if (personagem_ocupa_posicao(&jogo->fantasma_vermelho, x, y))
        return 0;

    if (personagem_ocupa_posicao(&jogo->fantasma_azul, x, y))
        return 0;

    if (personagem_ocupa_posicao(&jogo->fantasma_verde, x, y))
        return 0;

    if (personagem_ocupa_posicao(&jogo->fantasma_amarelo, x, y))
        return 0;

    return 1;
}

static void sorteia_posicao_personagens(void)
{
    static int inicializado = 0;

    if (!inicializado)
    {
        srand((unsigned int)time(NULL));
        inicializado = 1;
    }
}

/* ===================================================================
                         FUNCOES PRINCIPAIS
======================================================================*/

void inicializa_jogo(jogo_t *jogo)
{
    if (jogo == NULL)
        return;

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

    jogo->terminou = NAO;
    jogo->venceu = NAO;
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

            simbolo = token[0];

            if (eh_representacao_valida(simbolo) != SUCESSO)
            {
                fprintf(stderr,
                        "[ERRO] Simbolo invalido '%c' na posicao x=%d, y=%d. Use apenas '0' ou 'X'.\n",
                        simbolo,
                        x,
                        y);
                fclose(arquivo);
                return ERRO;
            }

            jogo->mapa[x][y] = simbolo;

            token = strtok(NULL, ";\r\n");
        }
    }

    fclose(arquivo);

    return SUCESSO;
}

int sorteia_posicao(const jogo_t *jogo, int *x, int *y)
{
    int total_livres = 0;
    int alvo;
    int contador = 0;

    if (jogo == NULL || x == NULL || y == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em sorteia_posicao.\n");
        return ERRO;
    }

    sorteia_posicao_personagens();

    for (int i = 0; i < LINHAS; i++)
    {
        for (int j = 0; j < COLUNAS; j++)
        {
            if (posicao_livre(jogo, i, j))
                total_livres++;
        }
    }

    if (total_livres == 0)
    {
        fprintf(stderr, "[ERRO] Nao ha posicao livre disponivel no mapa.\n");
        return ERRO;
    }

    alvo = rand() % total_livres;

    for (int i = 0; i < LINHAS; i++)
    {
        for (int j = 0; j < COLUNAS; j++)
        {
            if (posicao_livre(jogo, i, j))
            {
                if (contador == alvo)
                {
                    *x = i;
                    *y = j;
                    return SUCESSO;
                }

                contador++;
            }
        }
    }

    return ERRO;
}