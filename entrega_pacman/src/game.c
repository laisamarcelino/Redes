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

/* ===================================================================
                         FUNCOES AUXILIARES
======================================================================*/
/* Retorna SUCESSO se o símbolo representa uma célula válida para o mapa (parede ou vazio) */
static int eh_representacao_valida(char simbolo)
{
    // O arquivo CSV descreve apenas a estrutura fixa do mapa
    switch (simbolo)
    {
    case LAB_PAREDE:
    case LAB_VAZIO:
        return SUCESSO;

    default:
        return ERRO;
    }
}
/* Coloca a entidade em estado inativo, sem posição definida no mapa */
static void inicializa_entidade(entidade_t *entidade, char simbolo)
{
    if (entidade == NULL)
        return;
    entidade->posicao_x = -1;
    entidade->posicao_y = -1;
    entidade->ativo = 0;
    entidade->simbolo = simbolo;
    entidade->direcao = -1;
}

/* Retorna verdadeiro se (x, y) está dentro dos limites da grade do labirinto */
static int posicao_valida(int x, int y)
{
    return x >= 0 && x < LINHAS && y >= 0 && y < COLUNAS;
}
/* Retorna verdadeiro se a entidade está ativa e ocupa exatamente a célula (x, y) */
static int entidade_ocupa_posicao(const entidade_t *entidade, int x, int y)
{
    if (entidade == NULL)
        return 0;

    if (!entidade->ativo)
        return 0;

    return entidade->posicao_x == x && entidade->posicao_y == y;
}
/* Retorna verdadeiro se a célula (x, y) é um espaço vazio e nenhuma entidade a ocupa */
static int posicao_livre(const jogo_t *jogo, int x, int y)
{
    if (jogo == NULL)
        return 0;

    // As entidades são mantidas fora da matriz para preservar o mapa original do CSV.
    if (!posicao_valida(x, y))
        return 0;

    if (jogo->mapa[x][y] != LAB_VAZIO)
        return 0;

    if (entidade_ocupa_posicao(&jogo->pacman, x, y))
        return 0;

    if (entidade_ocupa_posicao(&jogo->fantasma_vermelho, x, y))
        return 0;

    if (entidade_ocupa_posicao(&jogo->fantasma_azul, x, y))
        return 0;

    if (entidade_ocupa_posicao(&jogo->fantasma_verde, x, y))
        return 0;

    if (entidade_ocupa_posicao(&jogo->fantasma_amarelo, x, y))
        return 0;

    for (int i = 0; i < TOTAL_PASTILHAS; i++)
    {
        if (entidade_ocupa_posicao(&jogo->pastilhas[i], x, y))
            return 0;
    }

    return 1;
}

/* Inicializa o gerador de números aleatórios com a hora atual, apenas uma vez por execução */
static void inicializa_srand(void)
{
    static int inicializado = 0;
    if (!inicializado)
    {
        srand((unsigned int)time(NULL));
        inicializado = 1;
    }
}
/* Retorna o índice da pastilha que ocupa a célula (x, y), ou -1 se não houver nenhuma */
static int pastilha_na_posicao(const jogo_t *jogo, int x, int y)
{
    if (jogo == NULL)
        return -1;

    for (int i = 0; i < TOTAL_PASTILHAS; i++)
    {
        if (entidade_ocupa_posicao(&jogo->pastilhas[i], x, y))
            return i;
    }

    return -1;
}
/* Retorna verdadeiro se qualquer fantasma ativo está na célula (x, y) */
static int fantasma_na_posicao(const jogo_t *jogo, int x, int y)
{
    if (jogo == NULL)
        return 0;

    return entidade_ocupa_posicao(&jogo->fantasma_vermelho, x, y) ||
           entidade_ocupa_posicao(&jogo->fantasma_azul, x, y) ||
           entidade_ocupa_posicao(&jogo->fantasma_verde, x, y) ||
           entidade_ocupa_posicao(&jogo->fantasma_amarelo, x, y);
}



/* ===================================================================
                         FUNCOES PRINCIPAIS
======================================================================*/
/* Zera toda a estrutura do jogo e coloca cada entidade em estado inativo */
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

    inicializa_entidade(&jogo->pacman, LAB_PACMAN);

    inicializa_entidade(&jogo->fantasma_vermelho, LAB_FANTASMA_VERMELHO);
    inicializa_entidade(&jogo->fantasma_azul, LAB_FANTASMA_AZUL);
    inicializa_entidade(&jogo->fantasma_verde, LAB_FANTASMA_VERDE);
    inicializa_entidade(&jogo->fantasma_amarelo, LAB_FANTASMA_AMARELO);

    inicializa_entidade(&jogo->pastilhas[0], LAB_PASTILHA_TXT_1);
    inicializa_entidade(&jogo->pastilhas[1], LAB_PASTILHA_TXT_2);
    inicializa_entidade(&jogo->pastilhas[2], LAB_PASTILHA_JPG_3);
    inicializa_entidade(&jogo->pastilhas[3], LAB_PASTILHA_JPG_4);
    inicializa_entidade(&jogo->pastilhas[4], LAB_PASTILHA_MP4_5);
    inicializa_entidade(&jogo->pastilhas[5], LAB_PASTILHA_MP4_6);
    jogo->rodada = 0;
    jogo->raio_visao = RAIO_INICIAL;
    jogo->pastilhas_coletadas = 0;
    jogo->ultima_pastilha_coletada = 0;

    jogo->terminou = 0;
    jogo->venceu = 0;

    jogo->verde_prefere_direita = 0;
}
/* Lê o arquivo CSV linha a linha e preenche a grade do labirinto com paredes e espaços vazios */
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

            // O CSV define apenas a estrutura fixa; entidades são sorteadas depois.
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
/* Escolhe aleatoriamente uma célula livre do mapa e escreve suas coordenadas em *x e *y */
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

    inicializa_srand();

    // A contagem antes do sorteio evita favorecer as primeiras casas livres do mapa.
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
/* Sorteia uma posição livre no mapa e ativa a entidade nela */
int posiciona_uma_entidade(jogo_t *jogo, entidade_t *entidade)
{
    int x;
    int y;

    if (jogo == NULL || entidade == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em posiciona_uma_entidade.\n");
        return ERRO;
    }
    entidade->ativo = 0;
    entidade->posicao_x = -1;
    entidade->posicao_y = -1;

    if (sorteia_posicao(jogo, &x, &y) != SUCESSO)
    {
        fprintf(stderr,
                "[ERRO] Nao foi possivel sortear posicao para entidade '%c'.\n",
                entidade->simbolo);
        return ERRO;
    }

    entidade->posicao_x = x;
    entidade->posicao_y = y;
    // A entidade só bloqueia a casa depois que o sorteio termina com sucesso.
    entidade->ativo = 1;

    return SUCESSO;
}
/* Distribui o Pac-Man, os quatro fantasmas e todas as pastilhas em posições aleatórias */
int posiciona_entidades_no_mapa(jogo_t *jogo)
{
    if (jogo == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em posiciona_entidades_no_mapa.\n");
        return ERRO;
    }

    if (posiciona_uma_entidade(jogo, &jogo->pacman) != SUCESSO)
        return ERRO;

    if (posiciona_uma_entidade(jogo, &jogo->fantasma_vermelho) != SUCESSO)
        return ERRO;

    if (posiciona_uma_entidade(jogo, &jogo->fantasma_azul) != SUCESSO)
        return ERRO;

    if (posiciona_uma_entidade(jogo, &jogo->fantasma_verde) != SUCESSO)
        return ERRO;

    if (posiciona_uma_entidade(jogo, &jogo->fantasma_amarelo) != SUCESSO)
        return ERRO;

    for (int i = 0; i < TOTAL_PASTILHAS; i++)
    {
        if (posiciona_uma_entidade(jogo, &jogo->pastilhas[i]) != SUCESSO)
            return ERRO;
    }
    jogo->fantasma_vermelho.direcao = DIRECAO_CIMA;
    jogo->fantasma_azul.direcao = DIRECAO_CIMA;
    jogo->fantasma_verde.direcao = DIRECAO_CIMA;
    jogo->fantasma_amarelo.direcao = DIRECAO_CIMA;

    return SUCESSO;
}

/* Grava no buffer a representação colorida (ANSI) de um único símbolo do labirinto */
// A visualização usa ANSI para diferenciar elementos sem alterar o protocolo de texto.
static size_t escreve_celula_colorida(char *saida, size_t usado, char simbolo)
{
    const char *cor;
    const char *texto;
    const char *r;

    switch (simbolo)
    {
    case LAB_PAREDE:
        cor = "\033[1;37m"; texto = "\xe2\x96\x88"; break;
    case LAB_PACMAN:
        cor = "\033[1;33m"; texto = "P";             break;
    case LAB_FANTASMA_VERMELHO:
        cor = "\033[31m";   texto = "R";             break;
    case LAB_FANTASMA_AZUL:
        cor = "\033[34m";   texto = "B";             break;
    case LAB_FANTASMA_VERDE:
        cor = "\033[32m";   texto = "G";             break;
    case LAB_FANTASMA_AMARELO:
        cor = "\033[33m";   texto = "Y";             break;
    case LAB_PASTILHA_TXT_1:
    case LAB_PASTILHA_TXT_2:
    case LAB_PASTILHA_JPG_3:
    case LAB_PASTILHA_JPG_4:
    case LAB_PASTILHA_MP4_5:
    case LAB_PASTILHA_MP4_6:
        cor = "\033[1;33m"; texto = "\xe2\x97\x8f"; break;
    default:
        saida[usado++] = ' ';
        return usado;
    }

    for (r = cor;       *r; r++) saida[usado++] = *r;
    for (r = texto;     *r; r++) saida[usado++] = *r;
    for (r = "\033[0m"; *r; r++) saida[usado++] = *r;

    return usado;
}
/* Monta a string completa do mapa para exibição no terminal, ocultando células além do raio de visão */
int gera_visualizacao(const jogo_t *jogo, char *saida, size_t capacidade, size_t *tamanho_saida)
{
    size_t usado = 0;

    if (jogo == NULL || saida == NULL || tamanho_saida == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em gera_visualizacao.\n");
        return ERRO;
    }

    if (capacidade < JOGO_VISUALIZACAO_MAX)
    {
        fprintf(stderr, "[ERRO] Buffer pequeno demais para visualizacao do mapa.\n");
        return ERRO;
    }

    int px    = jogo->pacman.posicao_x;
    int py    = jogo->pacman.posicao_y;
    int raio  = jogo->raio_visao;

    int escritos = snprintf(saida,
                            capacidade,
                            "Rodada %d | Raio de visao %d\n",
                            jogo->rodada,
                            jogo->raio_visao);

    if (escritos < 0 || (size_t)escritos >= capacidade)
    {
        fprintf(stderr, "[ERRO] Falha ao escrever cabecalho da visualizacao.\n");
        return ERRO;
    }

    usado = (size_t)escritos;

    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
            // O raio de visão limita a informação entregue ao cliente a cada rodada.
            if (abs(x - px) > raio || abs(y - py) > raio)
            {
                const char *nevoa = "\033[90m\xe2\x96\x91\033[0m";
                for (const char *r = nevoa; *r; r++) saida[usado++] = *r;
                continue;
            }

            char simbolo = jogo->mapa[x][y];

            if (entidade_ocupa_posicao(&jogo->pacman, x, y))
                simbolo = jogo->pacman.simbolo;
            else if (entidade_ocupa_posicao(&jogo->fantasma_vermelho, x, y))
                simbolo = jogo->fantasma_vermelho.simbolo;
            else if (entidade_ocupa_posicao(&jogo->fantasma_azul, x, y))
                simbolo = jogo->fantasma_azul.simbolo;
            else if (entidade_ocupa_posicao(&jogo->fantasma_verde, x, y))
                simbolo = jogo->fantasma_verde.simbolo;
            else if (entidade_ocupa_posicao(&jogo->fantasma_amarelo, x, y))
                simbolo = jogo->fantasma_amarelo.simbolo;
            else
            {
                for (int i = 0; i < TOTAL_PASTILHAS; i++)
                {
                    if (entidade_ocupa_posicao(&jogo->pastilhas[i], x, y))
                    {
                        simbolo = jogo->pastilhas[i].simbolo;
                        break;
                    }
                }
            }

            usado = escreve_celula_colorida(saida, usado, simbolo);
        }

        saida[usado++] = '\n';
    }

    saida[usado] = '\0';
    *tamanho_saida = usado;

    return SUCESSO;
}
/* Move o Pac-Man na direção indicada, coleta pastilhas e detecta colisão com fantasmas */
int movimenta_pacman(jogo_t *jogo, int deslocamento_x, int deslocamento_y)
{
    int novo_x;
    int novo_y;
    int indice_pastilha;

    if (jogo == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em movimenta_pacman.\n");
        return ERRO;
    }

    if (!jogo->pacman.ativo)
    {
        fprintf(stderr, "[ERRO] PacMan nao esta ativo no mapa.\n");
        return ERRO;
    }

    novo_x = jogo->pacman.posicao_x + deslocamento_x;
    novo_y = jogo->pacman.posicao_y + deslocamento_y;
    jogo->rodada++;

    if (jogo->rodada % 5 == 0)
    {
        jogo->raio_visao++;
    }

    if (!posicao_valida(novo_x, novo_y) || jogo->mapa[novo_x][novo_y] == LAB_PAREDE)
    {
        return SUCESSO;
    }

    jogo->pacman.posicao_x = novo_x;
    jogo->pacman.posicao_y = novo_y;

    indice_pastilha = pastilha_na_posicao(jogo, novo_x, novo_y);
    if (indice_pastilha >= 0)
    {
        jogo->ultima_pastilha_coletada = jogo->pastilhas[indice_pastilha].simbolo;
        jogo->pastilhas[indice_pastilha].ativo = 0;
        jogo->pastilhas_coletadas++;

        if (jogo->pastilhas_coletadas >= TOTAL_PASTILHAS)
        {
            jogo->terminou = 1;
            jogo->venceu = 1;
        }
    }

    if (fantasma_na_posicao(jogo, novo_x, novo_y))
    {
        jogo->terminou = 1;
        jogo->venceu = 0;
    }

    return SUCESSO;
}

/* ===================================================================
                     MOVIMENTACAO DOS FANTASMAS
======================================================================*/

/* Vetores de deslocamento para cada direção: cima, direita, baixo, esquerda */
// A ordem das direções serve de base para as regras de mão direita/esquerda.
static const int dx[4] = {-1,  0,  1,  0};
static const int dy[4] = { 0,  1,  0, -1};

/* Retorna verdadeiro se a célula (x, y) está dentro do mapa e não é uma parede */
static int passavel_fantasma(const jogo_t *jogo, int x, int y)
{
    return posicao_valida(x, y) && jogo->mapa[x][y] != LAB_PAREDE;
}

/* Move o fantasma para a próxima célula na direção dir e verifica colisão com o Pac-Man */
static void aplica_movimento_fantasma(jogo_t *jogo, entidade_t *f, int dir)
{
    int nx = f->posicao_x + dx[dir];
    int ny = f->posicao_y + dy[dir];

    f->direcao = dir;
    f->posicao_x = nx;
    f->posicao_y = ny;

    if (jogo->pacman.ativo &&
        jogo->pacman.posicao_x == nx &&
        jogo->pacman.posicao_y == ny)
    {
        jogo->terminou = 1;
        jogo->venceu = 0;
    }
}

/* Move o fantasma para a primeira direção da lista de prioridade que estiver livre */
// Cada fantasma fornece sua prioridade; a primeira direção livre é escolhida.
static void move_com_prioridade(jogo_t *jogo, entidade_t *f, const int prio[4])
{
    for (int i = 0; i < 4; i++)
    {
        int dir = prio[i];
        int nx = f->posicao_x + dx[dir];
        int ny = f->posicao_y + dy[dir];

        if (passavel_fantasma(jogo, nx, ny))
        {
            aplica_movimento_fantasma(jogo, f, dir);
            return;
        }
    }
}

/* Move o fantasma priorizando virar para o lado preferido antes de seguir em frente */
static void move_com_mao(jogo_t *jogo, entidade_t *f, int prefere_direita)
{
    int dir = (f->direcao < 0) ? DIRECAO_CIMA : f->direcao;
    int lado_preferido = prefere_direita ? (dir + 1) % 4 : (dir + 3) % 4;
    int lado_oposto = prefere_direita ? (dir + 3) % 4 : (dir + 1) % 4;
    int prio[4] = {
        lado_preferido,
        dir,
        lado_oposto,
        (dir + 2) % 4
    };

    move_com_prioridade(jogo, f, prio);
}

// Vermelho segue a regra da mão esquerda.
static void move_mao_esquerda(jogo_t *jogo, entidade_t *f)
{
    move_com_mao(jogo, f, 0);
}

// Azul segue a regra da mão direita.
static void move_mao_direita(jogo_t *jogo, entidade_t *f)
{
    move_com_mao(jogo, f, 1);
}

// Verde alterna entre as duas regras para não repetir sempre o mesmo padrão.
static void move_verde(jogo_t *jogo, entidade_t *f)
{
    if (jogo->verde_prefere_direita)
        move_mao_direita(jogo, f);
    else
        move_mao_esquerda(jogo, f);

    jogo->verde_prefere_direita = !jogo->verde_prefere_direita;
}

// Amarelo usa aleatoriedade para deixar uma ameaça menos previsível.
static void move_aleatorio(jogo_t *jogo, entidade_t *f)
{
    int validas[4];
    int n = 0;

    inicializa_srand();

    for (int d = 0; d < 4; d++)
    {
        int nx = f->posicao_x + dx[d];
        int ny = f->posicao_y + dy[d];
        if (passavel_fantasma(jogo, nx, ny))
            validas[n++] = d;
    }

    if (n == 0)
        return;

    aplica_movimento_fantasma(jogo, f, validas[rand() % n]);
}

/* Atualiza a posição de todos os fantasmas ativos, cada um com seu próprio comportamento */
void movimenta_fantasmas(jogo_t *jogo)
{
    if (jogo == NULL || jogo->terminou)
        return;

    if (jogo->fantasma_vermelho.ativo)
        move_mao_esquerda(jogo, &jogo->fantasma_vermelho);

    if (jogo->fantasma_azul.ativo && !jogo->terminou)
        move_mao_direita(jogo, &jogo->fantasma_azul);

    if (jogo->fantasma_verde.ativo && !jogo->terminou)
        move_verde(jogo, &jogo->fantasma_verde);

    if (jogo->fantasma_amarelo.ativo && !jogo->terminou)
        move_aleatorio(jogo, &jogo->fantasma_amarelo);
}
