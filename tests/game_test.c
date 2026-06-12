#include "game.h"

#include <stdio.h>

static int conta_pastilhas(const jogo_t *jogo)
{
    int total = 0;

    for (int i = 0; i < TOTAL_PASTILHAS; i++)
    {
        if (jogo->pastilhas[i].ativo)
            total++;
    }

    return total;
}

static int entidade_em_posicao_valida(const jogo_t *jogo, const entidade_t *entidade)
{
    return entidade->ativo &&
           entidade->posicao_x >= 0 &&
           entidade->posicao_x < LINHAS &&
           entidade->posicao_y >= 0 &&
           entidade->posicao_y < COLUNAS &&
           jogo->mapa[entidade->posicao_x][entidade->posicao_y] == LAB_VAZIO;
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

    if (posiciona_entidades_no_mapa(&jogo) != 0)
    {
        fprintf(stderr, "[ERRO] Nao posicionou entidades no mapa\n");
        return 1;
    }

    exibe_mapa(&jogo);

    if (!entidade_em_posicao_valida(&jogo, &jogo.pacman) ||
        !entidade_em_posicao_valida(&jogo, &jogo.fantasma_vermelho) ||
        !entidade_em_posicao_valida(&jogo, &jogo.fantasma_azul) ||
        !entidade_em_posicao_valida(&jogo, &jogo.fantasma_verde) ||
        !entidade_em_posicao_valida(&jogo, &jogo.fantasma_amarelo))
    {
        fprintf(stderr, "[ERRO] Entidade foi colocada em posicao invalida\n");
        return 1;
    }

    if (conta_pastilhas(&jogo) != TOTAL_PASTILHAS)
    {
        fprintf(stderr, "[ERRO] Mapa padrao deveria ter %d pastilhas\n", TOTAL_PASTILHAS);
        return 1;
    }

    int pacman_x = jogo.pacman.posicao_x;
    int pacman_y = jogo.pacman.posicao_y;

    if (movimenta_pacman(&jogo, 0, 0) != 0)
    {
        fprintf(stderr, "[ERRO] Movimento parado do PacMan falhou\n");
        return 1;
    }

    if (jogo.pacman.posicao_x != pacman_x ||
        jogo.pacman.posicao_y != pacman_y ||
        jogo.rodada != 1)
    {
        fprintf(stderr, "[ERRO] Movimento parado deveria manter posicao e contar rodada\n");
        return 1;
    }

    printf("[OK] Funcoes de mapa funcionando\n");
    return 0;
}
