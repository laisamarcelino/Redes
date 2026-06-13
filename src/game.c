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

/* APAGAR
 * Verifica se um simbolo lido do CSV pode ser usado na matriz do mapa.
 * Retorna SUCESSO para parede ou espaco vazio, e ERRO para qualquer outro
 * caractere.
 */
// Verifica se um simbolo é valido
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

/* APAGAR
 * A entidade comeca inativa e sem posicao valida ate ser posicionada no mapa.
 */
// Inicializa uma entidade do jogo com o simbolo informado.
static void inicializa_entidade(entidade_t *entidade, char simbolo)
{
    if (entidade == NULL)
        return;

    // Posicao -1 indica que a entidade ainda nao foi colocada no mapa
    entidade->posicao_x = -1;
    entidade->posicao_y = -1;
    entidade->ativo = 0;
    entidade->simbolo = simbolo;
    entidade->direcao = -1;
}

// Verifica se as coordenadas recebidas estao dentro dos limites da matriz do labirinto.
static int posicao_valida(int x, int y)
{
    return x >= 0 && x < LINHAS && y >= 0 && y < COLUNAS;
}

/* APAGAR
 * Entidades nulas ou inativas nunca contam como ocupantes.
 */
// Verifica se uma entidade ativa ocupa exatamente a posicao informada
static int entidade_ocupa_posicao(const entidade_t *entidade, int x, int y)
{
    if (entidade == NULL)
        return 0;

    if (!entidade->ativo)
        return 0;

    return entidade->posicao_x == x && entidade->posicao_y == y;
}

/* APAGAR
 * Verifica se uma casa pode receber uma nova entidade.
 * A casa precisa estar dentro do mapa, ser vazia e nao conter PacMan,
 * fantasmas ou pastilhas ativas.
 */
// Verifica se uma posição esta livre
static int posicao_livre(const jogo_t *jogo, int x, int y)
{
    if (jogo == NULL)
        return 0;

    // A posicao precisa existir, ser vazia no mapa e nao conter entidades
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

// Prepara o gerador de numeros aleatorios usado no sorteio das posicoes
static void inicializa_srand(void)
{
    static int inicializado = 0;

    // Inicializa o gerador pseudoaleatorio apenas uma vez por execucao
    if (!inicializado)
    {
        srand((unsigned int)time(NULL));
        inicializado = 1;
    }
}

/* APAGAR
 * Procura uma pastilha ativa em uma coordenada do mapa.
 * Retorna o indice da pastilha no vetor do jogo ou -1 quando a casa nao
 * possui pastilha ativa.
 */
// Procura uma pastilha ativa em uma coordenada do mapa
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

/* APAGAR
 * Verifica se qualquer fantasma ativo esta em uma coordenada.
 * Usado para detectar colisao depois que o PacMan se move.
 */
// Verifica se algum fantasma ativo ocupa a posicao informada.
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

/* APAGAR
 * Reinicia o estado completo do jogo.
 * O mapa fica vazio, todas as entidades ficam inativas e os contadores da
 * partida voltam para os valores iniciais.
 */
void inicializa_jogo(jogo_t *jogo)
{
    if (jogo == NULL)
        return;

    // Comeca com um mapa completamente vazio; o CSV pode sobrescrever depois
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

    // Estado inicial da partida
    jogo->rodada = 0;
    jogo->raio_visao = RAIO_INICIAL;
    jogo->pastilhas_coletadas = 0;
    jogo->ultima_pastilha_coletada = 0;

    jogo->terminou = 0;
    jogo->venceu = 0;
}

/* APAGAR
 * Carrega o labirinto a partir de um arquivo CSV 40x40.
 * Cada celula deve conter um simbolo valido de mapa; em caso de erro,
 * a funcao imprime uma mensagem e retorna ERRO.
 */
// Carrega o labirinto a partir de um arquivo CSV 40x40
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

    // Cada linha do CSV representa uma linha do labirinto 40x40
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

            // Sao aceitos somente parede e espaco vazio
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

/* APAGAR
 * Sorteia uma posicao livre do mapa e devolve as coordenadas por ponteiro.
 * O sorteio considera apenas casas vazias que nao estejam ocupadas por
 * nenhuma entidade ativa.
 */
// Sorteia uma posicao livre no mapa e devolve as coordenadas
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

    // Primeiro conta as casas candidatas para sortear uma delas de forma uniforme
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

    // Depois percorre novamente ate encontrar a casa correspondente ao sorteio
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

/* APAGAR
 * Sorteia uma posicao aleatoria livre e coloca a entidade nessa casa.
 * Antes do sorteio, a entidade e desativada para nao bloquear a propria
 * posicao antiga caso esteja sendo reposicionada.
 */
// Sorteia uma posicao aleatoria livre e coloca a entidade nessa casa
int posiciona_uma_entidade(jogo_t *jogo, entidade_t *entidade)
{
    int x;
    int y;

    if (jogo == NULL || entidade == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em posiciona_uma_entidade.\n");
        return ERRO;
    }

    /* APAGAR - entender
     * Garante que, caso a funcao seja chamada novamente,
     * a propria entidade nao bloqueie sua posicao antiga.
     */
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
    // A entidade so passa a bloquear a posicao depois de receber coordenadas validas
    entidade->ativo = 1;

    return SUCESSO;
}

/* APAGAR
 * Posiciona todas as entidades iniciais da partida.
 * A funcao coloca PacMan, fantasmas e pastilhas em casas livres, sem
 * sobreposicao, e define a direcao inicial dos fantasmas.
 */
// Posiciona todas as entidades iniciais da partida
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

    /* APAGAR - ALTERAR CONFORME REGRAS DOS FANTASMAS
     * Direcoes iniciais dos fantasmas.
     * Pode mudar depois conforme a regra de cada fantasma.
     */
    jogo->fantasma_vermelho.direcao = DIRECAO_CIMA;
    jogo->fantasma_azul.direcao = DIRECAO_CIMA;
    jogo->fantasma_verde.direcao = DIRECAO_CIMA;
    jogo->fantasma_amarelo.direcao = DIRECAO_CIMA;

    return SUCESSO;
}

/* APAGAR
 * Gera uma representacao textual completa do mapa atual.
 * O mapa base vem de jogo->mapa, mas entidades ativas aparecem por cima das
 * casas vazias para que o cliente veja as posicoes sorteadas.
 */
int gera_visualizacao(const jogo_t *jogo, char *saida, size_t capacidade, size_t *tamanho_saida)
{
    size_t usado = 0;

    if (jogo == NULL || saida == NULL || tamanho_saida == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro invalido em gera_visualizacao.\n");
        return ERRO;
    }

    if (capacidade < (LINHAS * (COLUNAS + 1)) + 1)
    {
        fprintf(stderr, "[ERRO] Buffer pequeno demais para visualizacao do mapa.\n");
        return ERRO;
    }

    for (int x = 0; x < LINHAS; x++)
    {
        for (int y = 0; y < COLUNAS; y++)
        {
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

            saida[usado++] = simbolo;
        }

        saida[usado++] = '\n';
    }

    saida[usado] = '\0';
    *tamanho_saida = usado;

    return SUCESSO;
}

/* APAGAR
 * Move o PacMan uma casa na direcao indicada pelo deslocamento.
 * Parede e fora do mapa bloqueiam o movimento; pastilhas sao coletadas ao
 * entrar na casa; colisao com fantasma encerra o jogo como derrota.
 */
// Movimenta pacman
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

    /* APAGAR
     * Movimento invalido nao muda a posicao, mas conta como rodada porque o
     * cliente enviou uma jogada.
     */
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
