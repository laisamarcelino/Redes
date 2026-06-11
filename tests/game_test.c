#include "game.h"

#include <stdio.h>

static int conta_pastilhas(const jogo_t *jogo)
{
    int total = 0;

    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
            if (jogo->mapa[x][y] >= LAB_PASTILHA_TXT_1 &&
                jogo->mapa[x][y] <= LAB_PASTILHA_MP4_6)
            {
                total++;
            }
        }
    }

    return total;
}

static int personagem_em_posicao_valida(const jogo_t *jogo, const personagem_t *personagem)
{
    return personagem->ativo &&
           personagem->posicao_x >= 0 &&
           personagem->posicao_x < LINHAS &&
           personagem->posicao_y >= 0 &&
           personagem->posicao_y < COLUNAS &&
           jogo->mapa[personagem->posicao_x][personagem->posicao_y] == LAB_VAZIO;
}

static char simbolo_na_posicao(const jogo_t *jogo, int x, int y)
{
    if (jogo->pacman.ativo &&
        jogo->pacman.posicao_x == x &&
        jogo->pacman.posicao_y == y)
    {
        return jogo->pacman.simbolo;
    }

    if (jogo->fantasma_vermelho.ativo &&
        jogo->fantasma_vermelho.posicao_x == x &&
        jogo->fantasma_vermelho.posicao_y == y)
    {
        return jogo->fantasma_vermelho.simbolo;
    }

    if (jogo->fantasma_azul.ativo &&
        jogo->fantasma_azul.posicao_x == x &&
        jogo->fantasma_azul.posicao_y == y)
    {
        return jogo->fantasma_azul.simbolo;
    }

    if (jogo->fantasma_verde.ativo &&
        jogo->fantasma_verde.posicao_x == x &&
        jogo->fantasma_verde.posicao_y == y)
    {
        return jogo->fantasma_verde.simbolo;
    }

    if (jogo->fantasma_amarelo.ativo &&
        jogo->fantasma_amarelo.posicao_x == x &&
        jogo->fantasma_amarelo.posicao_y == y)
    {
        return jogo->fantasma_amarelo.simbolo;
    }

    return jogo->mapa[x][y];
}

static void exibe_mapa(const jogo_t *jogo)
{
    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
            printf("%c", simbolo_na_posicao(jogo, x, y));
        }

        printf("\n");
    }
}

int main(void)
{
    jogo_t jogo;

    if (carrega_mapa_csv(&jogo, "maps/padrao_ufpr.csv") != 0)
    {
        fprintf(stderr, "[ERRO] Nao carregou maps/padrao_ufpr.csv\n");
        return 1;
    }

    if (jogo.pacman.ativo ||
        jogo.fantasma_vermelho.ativo ||
        jogo.fantasma_azul.ativo ||
        jogo.fantasma_verde.ativo ||
        jogo.fantasma_amarelo.ativo)
    {
        fprintf(stderr, "[ERRO] Mapa cru nao deveria ativar personagens\n");
        return 1;
    }

    inicializa_mapa_padrao(&jogo);
    exibe_mapa(&jogo);

    if (!personagem_em_posicao_valida(&jogo, &jogo.pacman) ||
        !personagem_em_posicao_valida(&jogo, &jogo.fantasma_vermelho) ||
        !personagem_em_posicao_valida(&jogo, &jogo.fantasma_azul) ||
        !personagem_em_posicao_valida(&jogo, &jogo.fantasma_verde) ||
        !personagem_em_posicao_valida(&jogo, &jogo.fantasma_amarelo))
    {
        fprintf(stderr, "[ERRO] Personagem foi colocado em posicao invalida\n");
        return 1;
    }

    if (conta_pastilhas(&jogo) != TOTAL_PASTILHAS)
    {
        fprintf(stderr, "[ERRO] Mapa padrao deveria ter %d pastilhas\n", TOTAL_PASTILHAS);
        return 1;
    }

    printf("[OK] Funcoes de mapa funcionando\n");
    return 0;
}
