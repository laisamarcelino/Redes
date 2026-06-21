#ifndef GAME_H
#define GAME_H

#include <stddef.h>
#include <stdint.h>

#define LINHAS 40
#define COLUNAS 40

#define TOTAL_PASTILHAS 6
#define RAIO_INICIAL 1

/* Limite para montar uma visualizacao textual completa do mapa.
 * Com ANSI + UTF-8 (ex: \033[1;37m + █(3 bytes) + \033[0m) cada celula usa
 * ate 15 bytes: 40*40*15 + 40 newlines + null = ~24041 bytes. */
#define JOGO_VISUALIZACAO_MAX 32768

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

/* Representa uma entidade (pacman, fantasma ou pastilha) */
typedef struct {
    int posicao_x;
    int posicao_y;
    int ativo;
    char simbolo;

    int direcao;
} entidade_t;

/*
 * Estado completo de uma partida.
 * O mapa guarda paredes, vazios e pastilhas; PacMan e fantasmas ficam
 * nas estruturas de entidade para facilitar movimentacao.
 */
typedef struct {
    char mapa[LINHAS][COLUNAS];

    entidade_t pacman;

    entidade_t fantasma_vermelho;
    entidade_t fantasma_azul;
    entidade_t fantasma_verde;
    entidade_t fantasma_amarelo;

    entidade_t pastilhas[TOTAL_PASTILHAS];

    int rodada;
    int raio_visao;
    int pastilhas_coletadas;

    int ultima_pastilha_coletada;

    int terminou;
    int venceu;

    int verde_prefere_direita;
} jogo_t;

/* Zera o estado do jogo e deixa o mapa todo vazio. */
void inicializa_jogo(jogo_t *jogo);

/*
 * Carrega um mapa 40x40 de um CSV
 * Aceita os simbolos definidos em representacao_labirinto_t
 */
int carrega_mapa_csv(jogo_t *jogo, const char *caminho_csv);

/*
 * Sorteia uma celula vazia, dentro dos limites do mapa e sem entidade
 * Retorna 0 em caso de sucesso e -1 se nao houver posicao disponivel
 */
int sorteia_posicao(const jogo_t *jogo, int *x, int *y);

// Sorteia e posiciona as entidades iniciais no mapa do jogo
int posiciona_uma_entidade(jogo_t *jogo, entidade_t *entidade);
int posiciona_entidades_no_mapa(jogo_t *jogo);

// Gera uma visualizacao completa do mapa atual do jogo.
int gera_visualizacao(const jogo_t *jogo, char *saida, size_t capacidade, size_t *tamanho_saida);

// Move o PacMan por um deslocamento de uma casa e atualiza o estado da rodada.
int movimenta_pacman(jogo_t *jogo, int deslocamento_x, int deslocamento_y);

// Move todos os fantasmas ativos de acordo com suas regras individuais.
void movimenta_fantasmas(jogo_t *jogo);

#endif
