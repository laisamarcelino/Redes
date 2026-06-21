#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"
#include "game.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/
uint8_t sequencia_esperada = 0;

#define CAMINHO_MAPA_PADRAO "maps/padrao_ufpr.csv"

/* APAGAR
 * Verifica se o tipo de mensagem pertence aos tipos usados para transmitir
 * arquivos de premio ou arquivos recebidos em blocos.
 */
// Retorna verdadeiro quando o tipo recebido representa um bloco de arquivo
static int tipo_arquivo(uint8_t tipo_msg)
{
    return tipo_msg == MSG_TXT || tipo_msg == MSG_JPG || tipo_msg == MSG_MP4;
}

/* APAGAR
 * Verifica se a mensagem recebida do cliente e uma jogada de movimento do
 * PacMan.
 */
// Retorna verdadeiro quando a mensagem representa um movimento do PacMan
static int tipo_movimento(uint8_t tipo_msg)
{
    return tipo_msg == MSG_MOV_CIMA ||
           tipo_msg == MSG_MOV_BAIXO ||
           tipo_msg == MSG_MOV_ESQUERDA ||
           tipo_msg == MSG_MOV_DIREITA;
}

/* APAGAR
 * Traduz o tipo de movimento do protocolo para deslocamento de linha e coluna
 * na matriz do labirinto.
 */
// Converte o tipo de movimento do protocolo para deslocamento no mapa
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

/* APAGAR
 * Retorna a extensao usada para salvar um arquivo recebido, de acordo com o
 * tipo de mensagem do protocolo.
 */
// Escolhe a extensao usada pelo servidor para salvar o arquivo recebido
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

/* APAGAR
 * Testa se um caminho ja existe no disco.
 * Usado para escolher um nome de saida sem sobrescrever arquivo anterior.
 */
// Verifica se um arquivo ja existe sem alterar seu conteudo
static int arquivo_existe(const char *caminho)
{
    FILE *arquivo = fopen(caminho, "rb");

    if (arquivo == NULL)
    {
        return 0;
    }

    fclose(arquivo);
    return 1;
}

/* APAGAR
 * Monta um caminho recebido_NNN.ext disponivel para salvar o proximo arquivo.
 * Retorna erro se a extensao nao for suportada, se o buffer for invalido ou se
 * todos os nomes ate recebido_999.ext ja existirem.
 */
// Monta um nome de saida livre para nao sobrescrever arquivos anteriores
static int monta_caminho_saida_arquivo(uint8_t tipo_msg, char *caminho_saida,
                                       size_t tamanho_caminho_saida)
{
    const char *extensao = extensao_saida_arquivo(tipo_msg);

    if (extensao == NULL || caminho_saida == NULL || tamanho_caminho_saida == 0)
    {
        return -1;
    }

    // Procura o primeiro nome recebido_NNN.ext que ainda nao existe
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

/* APAGAR
 * Imprime no terminal os campos de uma mensagem protocolada.
 * Serve para debug de pacotes recebidos que nao sao tratados por outro fluxo.
 */
// Função usada para debug
static void imprime_mensagem_protocolada(const mensagem_t *mensagem)
{
    if (mensagem == NULL)
    {
        return;
    }

    printf("[DEBUG] Tipo: %u\n", mensagem->tipo_msg);
    printf("[DEBUG] Sequencia: %u\n", mensagem->num_sequencia_msg);
    printf("[DEBUG] Tamanho dados: %u\n", mensagem->tamanho_dados);

    // Imprime o payload como texto quando possivel
    printf("[DEBUG] Dados: ");
    for (uint8_t i = 0; i < mensagem->tamanho_dados; i++)
    {
        unsigned char c = mensagem->dados[i];

        // Caracteres imprimiveis aparecem como texto normal
        if (c >= 32 && c <= 126)
        {
            putchar(c);
        }
        else
        {
            // Bytes nao imprimiveis aparecem em hexadecimal
            printf("\\x%02X", c);
        }
    }
    putchar('\n');
    // Garante que a saida apareça mesmo com o servidor em loop
    fflush(stdout);
}


/* APAGAR
 * Acrescenta um fragmento recebido ao buffer da transmissao atual.
 * A funcao realoca o buffer quando o espaco disponivel nao e suficiente.
 */
// Responsavel por remontar msgs fragmentadas (protocolo permite 31 bytes por msg)
static int remonta_mensagem(uint8_t **buffer, size_t *tamanho_atual,
                            size_t *capacidade, const uint8_t *dados,
                            size_t tamanho_dados)
{
    // Fragmento vazio nao altera a mensagem remontada
    if (tamanho_dados == 0)
    {
        return 0;
    }

    // Aumenta o buffer quando o fragmento nao cabe
    if (*tamanho_atual + tamanho_dados > *capacidade)
    {
        // Aumenta a capacidade para 128 bytes
        size_t nova_capacidade = (*capacidade == 0) ? 128 : *capacidade;

        // Dobra a capacidade ate caber o novo tamanho
        while (*tamanho_atual + tamanho_dados > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        // Realoca preservando os dados ja recebidos
        uint8_t *novo_buffer = realloc(*buffer, nova_capacidade);

        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao realocar buffer de recebimento\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    // Copia o fragmento no final da mensagem remontada
    memcpy(
        *buffer + *tamanho_atual,
        dados,
        tamanho_dados);

    // Atualiza o tamanho total ja recebido
    *tamanho_atual += tamanho_dados;

    return 0;
}

/* APAGAR
 * Imprime no terminal uma mensagem completa depois que todos os fragmentos
 * foram recebidos.
 */
// Imprime o buffer completo remontado
static void imprime_mensagem_completa(const uint8_t *buffer, size_t tamanho)
{
    // Imprime a mensagem remontada com todos os fragmentos
    printf("[DEBUG] Mensagem completa recebida com %zu bytes: ", tamanho);

    // Escreve os bytes exatamente como chegaram quando houver conteudo
    if (buffer != NULL && tamanho > 0)
    {
        fwrite(buffer, 1, tamanho, stdout);
    }

    printf("\n");
    fflush(stdout);
}

/* APAGAR
 * Salva no disco um arquivo recebido por transmissao fragmentada.
 * O nome de saida e escolhido automaticamente para evitar sobrescrita.
 */
// Grava o buffer remontado quando a transmissao recebida era de arquivo
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

    // Arquivos binarios precisam ser escritos exatamente byte a byte.
    if (tamanho > 0 && fwrite(buffer, 1, tamanho, arquivo) != tamanho)
    {
        perror("fwrite arquivo recebido");
        fclose(arquivo);
        return -1;
    }

    fclose(arquivo);

    printf("[DEBUG] Arquivo recebido salvo em %s com %zu bytes\n",
           caminho_saida,
           tamanho);

    return 0;
}

/* APAGAR
 * Gera a visualizacao textual do jogo e envia esse buffer ao cliente usando
 * fragmentacao do protocolo e controle de ACK/NACK.
 */
// Gera a visualizacao atual do jogo e envia o mapa completo ao cliente
static int envia_mapa_completo(int soquete, const jogo_t *jogo)
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
        soquete,
        MSG_VISUALIZACAO,
        (const uint8_t *)visualizacao,
        tamanho_visualizacao,
        &proxima_sequencia);
}

// Mapeia o simbolo da pastilha para o caminho e tipo do arquivo de premio.
static int caminho_arquivo_pastilha(char simbolo, const char **caminho, uint8_t *tipo)
{
    switch (simbolo)
    {
    case LAB_PASTILHA_TXT_1: *caminho = "pastilhas/1.txt"; *tipo = MSG_TXT; return 0;
    case LAB_PASTILHA_TXT_2: *caminho = "pastilhas/2.txt"; *tipo = MSG_TXT; return 0;
    case LAB_PASTILHA_JPG_3: *caminho = "pastilhas/3.jpg"; *tipo = MSG_JPG; return 0;
    case LAB_PASTILHA_JPG_4: *caminho = "pastilhas/4.jpg"; *tipo = MSG_JPG; return 0;
    case LAB_PASTILHA_MP4_5: *caminho = "pastilhas/5.mp4"; *tipo = MSG_MP4; return 0;
    case LAB_PASTILHA_MP4_6: *caminho = "pastilhas/6.mp4"; *tipo = MSG_MP4; return 0;
    default: return -1;
    }
}

// Envia o arquivo de premio da pastilha coletada e zera o campo no jogo.
static int envia_premio(int soquete, jogo_t *jogo)
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
    return envia_arquivo_protocolado(soquete, caminho, tipo, &seq);
}

// Envia o arquivo de colisao quando o PacMan encontra um fantasma.
static int envia_arquivo_colisao(int soquete)
{
    uint8_t seq = 0;
    return envia_arquivo_protocolado(
        soquete,
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
static int envia_resposta_completa(int soquete, jogo_t *jogo)
{
    // --- parte 1: arquivo ou sinal de "sem arquivo" ---
    if (jogo->terminou && !jogo->venceu)
    {
        jogo->ultima_pastilha_coletada = 0;
        envia_arquivo_colisao(soquete);
    }
    else if (jogo->ultima_pastilha_coletada != 0)
    {
        envia_premio(soquete, jogo);
    }
    else
    {
        uint8_t seq = 0;
        envia_buffer_protocolado(soquete, MSG_DADOS, NULL, 0, &seq);
    }

    // --- parte 2: status do jogo ---
    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));
    fim.tipo_msg      = MSG_FIM_JOGO;
    fim.tamanho_dados = 1;
    fim.dados[0]      = (uint8_t)(jogo->terminou
                            ? (jogo->venceu ? 1 : 2)
                            : 0);
    uint8_t seq = 0;
    return envia_pacote_com_reenvio(soquete, &fim, &seq);
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/*
 * Executa o loop principal do servidor
 * Inicializa o jogo, recebe pacotes do cliente, valida sequencia/CRC, trata
 * pedidos de mapa, movimentos, arquivos e mensagens comuns, e envia ACK/NACK
 */
int executa_servidor(int soquete, const char *caminho_mapa)
{
    // Buffer que recebe um pacote PacMan ja sem cabecalho Ethernet
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
        // Bloqueia ate chegar um pacote PacMan valido na camada de rede
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        // Trata erro de recebimento sem encerrar em interrupcoes de sinal
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        // Mensagem vazia não forma msg para o protocolo
        if (recebido == 0)
        {
            continue;
        }

        // Valida e transforma o pacote bruto em uma mensagem do protocolo
        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido\n");

            /* APAGAR
             *  Se o pacote falhou na validação, mas possui cabeçalho mínimo
             * e marcador de início correto, ainda conseguimos extrair a sequência
             * para enviar NACK.
             *
             * Isso cobre, por exemplo, pacotes com CRC inválido.
             */
            // Se falhar, o servidor manda NACK, se conseguir extrair a sequência do cabeçalho
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

        /* APAGAR
         * Se o servidor já aceitou esse pacote antes ele nao reprocessa o pacote
         * de novo, mas reenvia ACK. Isso evita duplicar os dados
         */
        // Reenvio da ultima sequencia ja aceita recebe ACK sem duplicar dados
        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada))
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        // Sequencia fora de ordem recebe NACK para pedir reenvio
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

        // Fragmentos de dados sao acumulados ate o fim da transmissao
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

        // Trata o fim da transmissao
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

            if (envia_mapa_completo(soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            // Envia FIM_TRANSMISSAO (sem arquivo) + MSG_FIM_JOGO=0 para sincronizar
            // o cliente interativo. Ver envia_resposta_completa.
            envia_resposta_completa(soquete, &jogo);

            /* APAGAR
             * O cliente atual inicia a sequencia em 0 a cada execucao.
             * Depois de responder ao pedido do mapa, o servidor aceita um
             * novo pedido independente tambem a partir da sequencia 0.
             */
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

            movimenta_fantasmas(&jogo);

            if (envia_mapa_completo(soquete, &jogo) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            // Envia arquivo (premio/colisao) ou sinal vazio + MSG_FIM_JOGO.
            // Ver envia_resposta_completa.
            envia_resposta_completa(soquete, &jogo);

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
            /* APAGAR
             * Cada execucao do cliente comeca a sequencia em 0.
             * Ao terminar uma transmissao completa, o servidor volta para 0
             * para aceitar um novo arquivo ou mensagem logo em seguida.
             */
            sequencia_esperada = 0;

            continue;
        }

        // Mostra no terminal mensagens de debug
        imprime_mensagem_protocolada(&mensagem);

        // Avança a sequencia
        sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

        // Confirma o recebimento da msg
        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);
    }
}
