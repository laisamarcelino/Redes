#ifndef GAME_H
#define GAME_H

#include <stddef.h>
#include <stdint.h>

#define LINHAS 40
#define COLUNAS 40

#define TOTAL_PASTILHAS 6
#define RAIO_INICIAL 1

/* Limite para montar uma visualizacao textual completa do mapa. */
#define JOGO_VISUALIZACAO_MAX 4096

/* Simbolos aceitos no arquivo CSV e usados internamente pelo jogo. */
typedef enum {
    LAB_PACMAN = 'P',
    LAB_PAREDE = 'X',
    LAB_VAZIO = '0',

    LAB_FANTASMA_VERMELHO = 'R',
    LAB_FANTASMA_AZUL = 'B',
    LAB_FANTASMA_VERDE = 'G',
    LAB_FANTASMA_AMARELO = 'Y',

    LAB_PASTILHA_TXT_1 = '1',
    LAB_PASTILHA_TXT_2 = '2',
    LAB_PASTILHA_JPG_3 = '3',
    LAB_PASTILHA_JPG_4 = '4',
    LAB_PASTILHA_MP4_5 = '5',
    LAB_PASTILHA_MP4_6 = '6'
} representacao_labirinto_t;

typedef enum {
    JOGO_CONTINUA = 0,
    JOGO_VITORIA = 1,
    JOGO_DERROTA = 2
} jogo_resultado_t;

/* Representa um personagem movel, separado do mapa base. */
typedef struct {
    int posicao_x;
    int posicao_y;
    int ativo;
    char simbolo;

    int direcao;
} personagem_t;

/*
 * Estado completo de uma partida.
 * O mapa guarda paredes, vazios e pastilhas; PacMan e fantasmas ficam
 * nas estruturas de personagem para facilitar movimentacao.
 */
typedef struct {
    char mapa[LINHAS][COLUNAS];

    personagem_t pacman;

    personagem_t fantasma_vermelho;
    personagem_t fantasma_azul;
    personagem_t fantasma_verde;
    personagem_t fantasma_amarelo;

    int rodada;
    int raio_visao;
    int pastilhas_coletadas;

    int ultima_pastilha_coletada;

    int terminou;
    int venceu;
} jogo_t;

/* Zera o estado do jogo e deixa o mapa todo vazio. */
void inicializa_jogo(jogo_t *jogo);

/*
 * Cria o mapa padrao com paredes e sorteia posicoes para PacMan,
 * fantasmas e pastilhas.
 */
void inicializa_mapa_padrao(jogo_t *jogo);

/*
 * Carrega um mapa 40x40 de um CSV separado por ';'.
 * Aceita os simbolos definidos em representacao_labirinto_t.
 */
int carrega_mapa_csv(jogo_t *jogo, const char *caminho_csv);

/*
 * Sorteia uma celula vazia, dentro dos limites do mapa e sem personagem.
 * Retorna 0 em caso de sucesso e -1 se nao houver posicao disponivel.
 */
int sorteia_posicao(const jogo_t *jogo, int *x, int *y);

/*
int aplica_movimento_pacman(
    jogo_t *jogo,
    uint8_t movimento,
    int *pastilha_coletada
);

void move_fantasmas(jogo_t *jogo);

jogo_resultado_t executa_rodada(
    jogo_t *jogo,
    uint8_t movimento,
    int *pastilha_coletada
);

int gera_visualizacao(
    const jogo_t *jogo,
    char *saida,
    size_t tamanho_saida
);

int jogo_colidiu_fantasma(const jogo_t *jogo);
int jogo_acabou(const jogo_t *jogo);

const char *jogo_caminho_premio(int numero_pastilha);
*/
#endif
