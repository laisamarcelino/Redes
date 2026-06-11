#define _POSIX_C_SOURCE 200809L

#include "game.h"
#include "protocol.h"

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

static int deslocamento_por_direcao(int direcao, int *dx, int *dy)
{
    if (dx == NULL || dy == NULL)
    {
        return ERRO;
    }

    *dx = 0;
    *dy = 0;

    switch (direcao)
    {
    case DIRECAO_CIMA:
        *dx = -1;
        return SUCESSO;
    case DIRECAO_DIREITA:
        *dy = 1;
        return SUCESSO;
    case DIRECAO_BAIXO:
        *dx = 1;
        return SUCESSO;
    case DIRECAO_ESQUERDA:
        *dy = -1;
        return SUCESSO;
    default:
        return ERRO;
    }
}

static int direcao_por_movimento(uint8_t movimento)
{
    switch (movimento)
    {
    case MSG_MOV_CIMA:
        return DIRECAO_CIMA;
    case MSG_MOV_DIREITA:
        return DIRECAO_DIREITA;
    case MSG_MOV_BAIXO:
        return DIRECAO_BAIXO;
    case MSG_MOV_ESQUERDA:
        return DIRECAO_ESQUERDA;
    default:
        return ERRO;
    }
}

static int celula_bloqueada(const jogo_t *jogo, int x, int y)
{
    return jogo == NULL ||
           !posicao_valida_mapa(x, y) ||
           jogo->mapa[x][y] == LAB_PAREDE;
}

static int celula_ocupada_por_outro_fantasma(
    const jogo_t *jogo,
    const personagem_t *fantasma,
    int x,
    int y)
{
    const personagem_t *fantasmas[] = {
        &jogo->fantasma_vermelho,
        &jogo->fantasma_azul,
        &jogo->fantasma_verde,
        &jogo->fantasma_amarelo};

    for (size_t i = 0; i < sizeof(fantasmas) / sizeof(fantasmas[0]); i++)
    {
        if (fantasmas[i] == fantasma)
        {
            continue;
        }

        if (personagem_ocupa_posicao(fantasmas[i], x, y))
        {
            return 1;
        }
    }

    return 0;
}

static int pode_mover_fantasma(const jogo_t *jogo, const personagem_t *fantasma, int direcao)
{
    int dx;
    int dy;
    int novo_x;
    int novo_y;

    if (fantasma == NULL || !fantasma->ativo ||
        deslocamento_por_direcao(direcao, &dx, &dy) != SUCESSO)
    {
        return 0;
    }

    novo_x = fantasma->posicao_x + dx;
    novo_y = fantasma->posicao_y + dy;

    return !celula_bloqueada(jogo, novo_x, novo_y) &&
           !celula_ocupada_por_outro_fantasma(jogo, fantasma, novo_x, novo_y);
}

static void move_fantasma_para_direcao(personagem_t *fantasma, int direcao)
{
    int dx;
    int dy;

    if (fantasma == NULL ||
        deslocamento_por_direcao(direcao, &dx, &dy) != SUCESSO)
    {
        return;
    }

    fantasma->posicao_x += dx;
    fantasma->posicao_y += dy;
    fantasma->direcao = direcao;
}

static int escolhe_direcao_regra_mao(
    const jogo_t *jogo,
    const personagem_t *fantasma,
    int sentido)
{
    int atual = fantasma->direcao;

    /*
     * Ordem local da regra:
     *   sentido -1 tenta esquerda, frente, direita e volta.
     *   sentido  1 tenta direita, frente, esquerda e volta.
     */
    int tentativas[4] = {
        (atual + sentido + 4) % 4,
        atual,
        (atual - sentido + 4) % 4,
        (atual + 2) % 4};

    for (int i = 0; i < 4; i++)
    {
        if (pode_mover_fantasma(jogo, fantasma, tentativas[i]))
        {
            return tentativas[i];
        }
    }

    return atual;
}

static int escolhe_direcao_aleatoria(const jogo_t *jogo, const personagem_t *fantasma)
{
    int direcoes[4];
    int quantidade = 0;

    inicializa_gerador_aleatorio();

    for (int direcao = 0; direcao < 4; direcao++)
    {
        if (pode_mover_fantasma(jogo, fantasma, direcao))
        {
            direcoes[quantidade] = direcao;
            quantidade++;
        }
    }

    if (quantidade == 0)
    {
        return fantasma->direcao;
    }

    return direcoes[rand() % quantidade];
}

static void move_um_fantasma(jogo_t *jogo, personagem_t *fantasma, int modo)
{
    int direcao;

    if (jogo == NULL || fantasma == NULL || !fantasma->ativo)
    {
        return;
    }

    if (modo == LAB_FANTASMA_VERMELHO)
    {
        direcao = escolhe_direcao_regra_mao(jogo, fantasma, -1);
    }
    else if (modo == LAB_FANTASMA_AZUL)
    {
        direcao = escolhe_direcao_regra_mao(jogo, fantasma, 1);
    }
    else if (modo == LAB_FANTASMA_VERDE)
    {
        /* O verde alterna a prioridade entre esquerda e direita a cada rodada. */
        int sentido = (jogo->rodada % 2 == 0) ? -1 : 1;
        direcao = escolhe_direcao_regra_mao(jogo, fantasma, sentido);
    }
    else
    {
        direcao = escolhe_direcao_aleatoria(jogo, fantasma);
    }

    if (pode_mover_fantasma(jogo, fantasma, direcao))
    {
        move_fantasma_para_direcao(fantasma, direcao);
    }
}

static char simbolo_visivel_na_posicao(const jogo_t *jogo, int x, int y)
{
    if (personagem_ocupa_posicao(&jogo->pacman, x, y))
    {
        return jogo->pacman.simbolo;
    }

    if (personagem_ocupa_posicao(&jogo->fantasma_vermelho, x, y))
    {
        return jogo->fantasma_vermelho.simbolo;
    }

    if (personagem_ocupa_posicao(&jogo->fantasma_azul, x, y))
    {
        return jogo->fantasma_azul.simbolo;
    }

    if (personagem_ocupa_posicao(&jogo->fantasma_verde, x, y))
    {
        return jogo->fantasma_verde.simbolo;
    }

    if (personagem_ocupa_posicao(&jogo->fantasma_amarelo, x, y))
    {
        return jogo->fantasma_amarelo.simbolo;
    }

    return jogo->mapa[x][y];
}

static int adiciona_texto(char *saida, size_t tamanho_saida, size_t *usado,
                         const char *formato, ...)
{
    va_list argumentos;
    int escritos;

    if (saida == NULL || usado == NULL || *usado >= tamanho_saida)
    {
        return ERRO;
    }

    va_start(argumentos, formato);
    escritos = vsnprintf(saida + *usado, tamanho_saida - *usado, formato, argumentos);
    va_end(argumentos);

    if (escritos < 0 || (size_t)escritos >= tamanho_saida - *usado)
    {
        return ERRO;
    }

    *usado += (size_t)escritos;
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

int aplica_movimento_pacman(
    jogo_t *jogo,
    uint8_t movimento,
    int *pastilha_coletada)
{
    int direcao;
    int dx;
    int dy;
    int novo_x;
    int novo_y;
    char destino;

    if (pastilha_coletada != NULL)
    {
        *pastilha_coletada = 0;
    }

    if (jogo == NULL || !jogo->pacman.ativo || jogo->terminou)
    {
        return ERRO;
    }

    direcao = direcao_por_movimento(movimento);
    if (direcao == ERRO ||
        deslocamento_por_direcao(direcao, &dx, &dy) != SUCESSO)
    {
        return ERRO;
    }

    novo_x = jogo->pacman.posicao_x + dx;
    novo_y = jogo->pacman.posicao_y + dy;

    /* Movimento contra parede ou fora do mapa apenas consome a rodada. */
    if (celula_bloqueada(jogo, novo_x, novo_y))
    {
        return SUCESSO;
    }

    jogo->pacman.posicao_x = novo_x;
    jogo->pacman.posicao_y = novo_y;
    jogo->pacman.direcao = direcao;

    destino = jogo->mapa[novo_x][novo_y];
    if (destino >= LAB_PASTILHA_TXT_1 && destino <= LAB_PASTILHA_MP4_6)
    {
        jogo->ultima_pastilha_coletada = destino - '0';
        jogo->pastilhas_coletadas++;
        jogo->mapa[novo_x][novo_y] = LAB_VAZIO;

        if (pastilha_coletada != NULL)
        {
            *pastilha_coletada = jogo->ultima_pastilha_coletada;
        }
    }

    return SUCESSO;
}

void move_fantasmas(jogo_t *jogo)
{
    if (jogo == NULL || jogo->terminou)
    {
        return;
    }

    move_um_fantasma(jogo, &jogo->fantasma_vermelho, LAB_FANTASMA_VERMELHO);
    move_um_fantasma(jogo, &jogo->fantasma_azul, LAB_FANTASMA_AZUL);
    move_um_fantasma(jogo, &jogo->fantasma_verde, LAB_FANTASMA_VERDE);
    move_um_fantasma(jogo, &jogo->fantasma_amarelo, LAB_FANTASMA_AMARELO);
}

jogo_resultado_t executa_rodada(
    jogo_t *jogo,
    uint8_t movimento,
    int *pastilha_coletada)
{
    if (jogo == NULL)
    {
        return JOGO_DERROTA;
    }

    if (aplica_movimento_pacman(jogo, movimento, pastilha_coletada) != SUCESSO)
    {
        return JOGO_CONTINUA;
    }

    if (jogo_colidiu_fantasma(jogo))
    {
        jogo->terminou = 1;
        jogo->venceu = 0;
        return JOGO_DERROTA;
    }

    move_fantasmas(jogo);

    jogo->rodada++;
    jogo->raio_visao = RAIO_INICIAL + (jogo->rodada / 5);

    if (jogo_colidiu_fantasma(jogo))
    {
        jogo->terminou = 1;
        jogo->venceu = 0;
        return JOGO_DERROTA;
    }

    if (jogo->pastilhas_coletadas >= TOTAL_PASTILHAS)
    {
        jogo->terminou = 1;
        jogo->venceu = 1;
        return JOGO_VITORIA;
    }

    return JOGO_CONTINUA;
}

int gera_visualizacao(
    const jogo_t *jogo,
    char *saida,
    size_t tamanho_saida)
{
    size_t usado = 0;
    int inicio_x;
    int fim_x;
    int inicio_y;
    int fim_y;

    if (jogo == NULL || saida == NULL || tamanho_saida == 0)
    {
        return ERRO;
    }

    saida[0] = '\0';

    if (adiciona_texto(
            saida,
            tamanho_saida,
            &usado,
            "Rodada: %d | Raio: %d | Pastilhas: %d/%d\n",
            jogo->rodada,
            jogo->raio_visao,
            jogo->pastilhas_coletadas,
            TOTAL_PASTILHAS) != SUCESSO)
    {
        return ERRO;
    }

    if (jogo->terminou)
    {
        if (adiciona_texto(
                saida,
                tamanho_saida,
                &usado,
                "Resultado: %s\n",
                jogo->venceu ? "VITORIA" : "DERROTA") != SUCESSO)
        {
            return ERRO;
        }
    }

    if (jogo->ultima_pastilha_coletada > 0)
    {
        if (adiciona_texto(
                saida,
                tamanho_saida,
                &usado,
                "Pastilha coletada: %d\n",
                jogo->ultima_pastilha_coletada) != SUCESSO)
        {
            return ERRO;
        }
    }

    inicio_x = jogo->pacman.posicao_x - jogo->raio_visao;
    fim_x = jogo->pacman.posicao_x + jogo->raio_visao;
    inicio_y = jogo->pacman.posicao_y - jogo->raio_visao;
    fim_y = jogo->pacman.posicao_y + jogo->raio_visao;

    /* A visualizacao enviada ao cliente e apenas o recorte que o PacMan enxerga. */
    for (int x = inicio_x; x <= fim_x; x++)
    {
        for (int y = inicio_y; y <= fim_y; y++)
        {
            char simbolo = LAB_PAREDE;

            if (posicao_valida_mapa(x, y))
            {
                simbolo = simbolo_visivel_na_posicao(jogo, x, y);
            }

            if (adiciona_texto(saida, tamanho_saida, &usado, "%c", simbolo) != SUCESSO)
            {
                return ERRO;
            }
        }

        if (adiciona_texto(saida, tamanho_saida, &usado, "\n") != SUCESSO)
        {
            return ERRO;
        }
    }

    return SUCESSO;
}

int jogo_colidiu_fantasma(const jogo_t *jogo)
{
    if (jogo == NULL || !jogo->pacman.ativo)
    {
        return 0;
    }

    return personagem_ocupa_posicao(&jogo->fantasma_vermelho,
                                    jogo->pacman.posicao_x,
                                    jogo->pacman.posicao_y) ||
           personagem_ocupa_posicao(&jogo->fantasma_azul,
                                     jogo->pacman.posicao_x,
                                     jogo->pacman.posicao_y) ||
           personagem_ocupa_posicao(&jogo->fantasma_verde,
                                     jogo->pacman.posicao_x,
                                     jogo->pacman.posicao_y) ||
           personagem_ocupa_posicao(&jogo->fantasma_amarelo,
                                     jogo->pacman.posicao_x,
                                     jogo->pacman.posicao_y);
}

int jogo_acabou(const jogo_t *jogo)
{
    return jogo != NULL && jogo->terminou;
}

const char *jogo_caminho_premio(int numero_pastilha)
{
    switch (numero_pastilha)
    {
    case 1:
        return "assets/1.txt";
    case 2:
        return "assets/2.txt";
    case 3:
        return "assets/3.jpg";
    case 4:
        return "assets/4.jpg";
    case 5:
        return "assets/5.mp4";
    case 6:
        return "assets/6.mp4";
    default:
        return NULL;
    }
}
