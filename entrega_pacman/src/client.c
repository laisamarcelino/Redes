#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

#define TIMEOUT_RESPOSTA_MS 1000
#define PASTA_RECEBIDOS "recebidos"

static uint8_t proxima_sequencia_cliente = 0;

/* Converte um texto (ex: "cima", "w") no tipo de mensagem de movimento correspondente */
static uint8_t tipo_movimento_por_texto(const char *mensagem)
{
    if (mensagem == NULL)
    {
        return MSG_ERRO;
    }

    if (strcmp(mensagem, "cima") == 0 || strcmp(mensagem, "w") == 0)
        return MSG_MOV_CIMA;

    if (strcmp(mensagem, "baixo") == 0 || strcmp(mensagem, "s") == 0)
        return MSG_MOV_BAIXO;

    if (strcmp(mensagem, "esquerda") == 0 || strcmp(mensagem, "a") == 0)
        return MSG_MOV_ESQUERDA;

    if (strcmp(mensagem, "direita") == 0 || strcmp(mensagem, "d") == 0)
        return MSG_MOV_DIREITA;

    return MSG_ERRO;
}

/* Acrescenta dados ao buffer dinâmico, expandindo sua capacidade se necessário */
static int anexa_buffer(uint8_t **buffer, size_t *tamanho_atual,
                        size_t *capacidade, const uint8_t *dados,
                        size_t tamanho_dados, int termina_com_zero)
{
    size_t extra_zero = termina_com_zero ? 1 : 0;

    if (tamanho_dados == 0)
    {
        return 0;
    }

    if (*tamanho_atual + tamanho_dados + extra_zero > *capacidade)
    {
        size_t nova_capacidade = (*capacidade == 0) ? 256 : *capacidade;

        while (*tamanho_atual + tamanho_dados + extra_zero > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        uint8_t *novo_buffer = realloc(*buffer, nova_capacidade);
        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao realocar buffer recebido\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    memcpy(*buffer + *tamanho_atual, dados, tamanho_dados);
    *tamanho_atual += tamanho_dados;

    if (termina_com_zero)
    {
        (*buffer)[*tamanho_atual] = '\0';
    }

    return 0;
}

/* Envia ao servidor uma solicitação para receber o estado atual do mapa */
static int envia_pedido_mapa(int *p_soquete)
{
    mensagem_t mensagem;

    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo_msg = MSG_INICIALIZACAO;
    mensagem.tamanho_dados = 0;

    return envia_pacote_com_reenvio(
        p_soquete,
        &mensagem,
        &proxima_sequencia_cliente);
}

/* Envia ao servidor o comando de movimento escolhido pelo jogador */
static int envia_movimento_pacman(int *p_soquete, uint8_t tipo_movimento)
{
    mensagem_t mensagem;

    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo_msg = tipo_movimento;
    mensagem.tamanho_dados = 0;

    return envia_pacote_com_reenvio(
        p_soquete,
        &mensagem,
        &proxima_sequencia_cliente);
}

/* Aguarda a chegada de um pacote, reiniciando a espera em caso de interrupção por sinal */
static ssize_t espera_pacote_cliente(int *p_soquete, uint8_t *pacote, size_t tamanho_pacote)
{
    while (1)
    {
        ssize_t recebido = espera_mensagem_timeout(
            p_soquete,
            pacote,
            tamanho_pacote,
            TIMEOUT_RESPOSTA_MS);

        if (recebido == -1 && errno == EINTR)
        {
            continue;
        }

        return recebido;
    }
}

/* Recebe e valida uma mensagem do servidor, descartando duplicatas e pacotes fora de ordem */
static int recebe_mensagem_cliente(int *p_soquete, mensagem_t *mensagem,
                                   uint8_t *sequencia_esperada)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];

    while (1)
    {
        ssize_t recebido = espera_pacote_cliente(p_soquete, pacote, sizeof(pacote));

        if (recebido == REDE_TIMEOUT)
        {
            return REDE_TIMEOUT;
        }

        if (recebido < 0)
        {
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(pacote, (size_t)recebido, mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido pelo cliente\n");

            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                envia_ack_nack(
                    *p_soquete,
                    MSG_NACK,
                    extrai_sequencia_pacote_bruto(pacote));
            }

            continue;
        }

        log_mensagem("SRV", mensagem);

        if (mensagem->num_sequencia_msg ==
            calcula_sequencia_anterior(*sequencia_esperada))
        {
            envia_ack_nack(
                *p_soquete,
                MSG_ACK,
                mensagem->num_sequencia_msg);
            continue;
        }

        if (mensagem->num_sequencia_msg != *sequencia_esperada)
        {
            envia_ack_nack(
                *p_soquete,
                MSG_NACK,
                mensagem->num_sequencia_msg);
            continue;
        }

        return 0;
    }
}

/* Recebe todos os fragmentos do mapa enviados pelo servidor e exibe o resultado no terminal */
static int recebe_mapa_completo(int *p_soquete)
{
    mensagem_t mensagem;
    uint8_t *visualizacao = NULL;
    size_t tamanho_visualizacao = 0;
    size_t capacidade_visualizacao = 0;
    uint8_t sequencia_esperada = 0;

    while (1)
    {
        int recebido = recebe_mensagem_cliente(
            p_soquete,
            &mensagem,
            &sequencia_esperada);

        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr, "[AVISO] Timeout esperando mapa do servidor\n");
            free(visualizacao);
            return REDE_TIMEOUT;
        }

        if (recebido < 0)
        {
            perror("espera_mensagem_timeout");
            free(visualizacao);
            return -1;
        }

        if (mensagem.tipo_msg == MSG_VISUALIZACAO)
        {
            if (anexa_buffer(
                    &visualizacao,
                    &tamanho_visualizacao,
                    &capacidade_visualizacao,
                    mensagem.dados,
                    mensagem.tamanho_dados,
                    1) != 0)
            {
                free(visualizacao);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                *p_soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(
                *p_soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (visualizacao != NULL)
            {
                printf("%s", visualizacao);
            }

            free(visualizacao);
            return 0;
        }

        fprintf(stderr, "[ERRO] Tipo inesperado ao receber mapa: %u\n",
                mensagem.tipo_msg);
        envia_ack_nack(
            *p_soquete,
            MSG_NACK,
            mensagem.num_sequencia_msg);
    }
}

/* Abre o arquivo informado com o visualizador padrão do sistema operacional */
static void abre_arquivo_externo(const char *caminho)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork xdg-open");
        return;
    }

    if (pid == 0)
    {
        execlp("xdg-open", "xdg-open", caminho, (char *)NULL);
        perror("xdg-open");
        _exit(1);
    }
    // O jogo continua enquanto o visualizador externo abre o prêmio.
}


/* Salva o conteúdo do buffer em um arquivo numerado sequencialmente dentro da pasta recebidos */
static int salva_em_recebidos(const uint8_t *buffer, size_t tamanho,
                              const char *ext, char *caminho, size_t tam_caminho)
{
    for (int i = 1; i <= 999; i++)
    {
        int escritos = snprintf(caminho,
                                tam_caminho,
                                "%s/premio_%03d.%s",
                                PASTA_RECEBIDOS,
                                i,
                                ext);

        if (escritos < 0 || (size_t)escritos >= tam_caminho)
        {
            fprintf(stderr, "[ERRO] Caminho de premio muito longo\n");
            return -1;
        }

        struct stat st;
        if (stat(caminho, &st) == 0)
        {
            continue;
        }

        if (errno != ENOENT)
        {
            perror("stat premio");
            return -1;
        }

        FILE *f = fopen(caminho, "wb");
        if (f == NULL) { perror("fopen premio"); return -1; }

        if (tamanho > 0 && fwrite(buffer, 1, tamanho, f) != tamanho)
        {
            perror("fwrite premio");
            fclose(f);
            return -1;
        }

        if (fclose(f) != 0)
        {
            perror("fclose premio");
            return -1;
        }

        return 0;
    }

    fprintf(stderr, "[AVISO] Nao foi possivel salvar o arquivo de premio\n");
    return -1;
}

/* Salva o arquivo de prêmio recebido e o exibe (texto no terminal, mídia no visualizador externo) */
static void exibe_arquivo_recebido(uint8_t tipo, const uint8_t *buffer, size_t tamanho)
{
    const char *ext = (tipo == MSG_TXT) ? "txt" : (tipo == MSG_JPG) ? "jpg" : "mp4";
    char caminho[128];

    if (salva_em_recebidos(buffer, tamanho, ext, caminho, sizeof(caminho)) != 0)
        return;

    if (tipo == MSG_TXT)
    {
        printf("\n=== Premio recebido (texto) — salvo em %s ===\n", caminho);
        fwrite(buffer, 1, tamanho, stdout);
        printf("\n=============================================\n");
    }
    else
    {
        printf("\n=== Premio salvo em: %s — abrindo... ===\n", caminho);
        abre_arquivo_externo(caminho);
    }
}

/*
 * Recebe o arquivo enviado pelo servidor logo apos o mapa.
 * Se a resposta se perder durante uma reconexao, o timeout evita travar o jogo.
 */
static int recebe_arquivo_se_disponivel(int *p_soquete)
{
    mensagem_t mensagem;
    uint8_t sequencia_esperada = 0;
    uint8_t *buffer = NULL;
    size_t tamanho = 0;
    size_t capacidade = 0;
    uint8_t tipo_atual = MSG_DADOS;

    while (1)
    {
        int recebido = recebe_mensagem_cliente(
            p_soquete,
            &mensagem,
            &sequencia_esperada);

        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr, "[AVISO] Timeout esperando arquivo do servidor\n");
            free(buffer);
            return REDE_TIMEOUT;
        }

        if (recebido < 0)
        {
            free(buffer);
            return -1;
        }

        // O servidor envia FIM_TRANSMISSAO mesmo quando não existe prêmio.
        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(*p_soquete, MSG_ACK, mensagem.num_sequencia_msg);
            if (buffer != NULL)
                exibe_arquivo_recebido(tipo_atual, buffer, tamanho);
            free(buffer);
            return 0;
        }

        if (mensagem.tipo_msg != MSG_TXT &&
            mensagem.tipo_msg != MSG_JPG &&
            mensagem.tipo_msg != MSG_MP4)
        {
            envia_ack_nack(*p_soquete, MSG_NACK, mensagem.num_sequencia_msg);
            free(buffer);
            return -1;
        }

        tipo_atual = mensagem.tipo_msg;

        if (anexa_buffer(
                &buffer,
                &tamanho,
                &capacidade,
                mensagem.dados,
                mensagem.tamanho_dados,
                0) != 0)
        {
            free(buffer);
            return -1;
        }

        envia_ack_nack(*p_soquete, MSG_ACK, mensagem.num_sequencia_msg);
        sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);
    }
}

/* Aguarda a mensagem de encerramento do jogo e retorna o código de resultado da partida */
static int recebe_fim_jogo(int *p_soquete)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;

    while (1)
    {
        ssize_t recebido = espera_pacote_cliente(p_soquete, pacote, sizeof(pacote));

        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr, "[AVISO] Timeout esperando estado final do jogo\n");
            return REDE_TIMEOUT;
        }

        if (recebido < 0)
        {
            return -1;
        }

        if (recebido == 0) continue;

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            if ((size_t)recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
                envia_ack_nack(*p_soquete, MSG_NACK,
                               extrai_sequencia_pacote_bruto(pacote));
            continue;
        }

        log_mensagem("SRV", &mensagem);

        if (mensagem.tipo_msg != MSG_FIM_JOGO) continue;

        envia_ack_nack(*p_soquete, MSG_ACK, mensagem.num_sequencia_msg);

        return (mensagem.tamanho_dados >= 1) ? (int)mensagem.dados[0] : 0;
    }
}

/* Recebe o arquivo de prêmio (se houver) seguido do resultado final da rodada */
static int recebe_resposta_completa(int *p_soquete)
{
    int resultado = recebe_arquivo_se_disponivel(p_soquete);
    if (resultado != 0)
    {
        return resultado;
    }

    return recebe_fim_jogo(p_soquete);
}

/* Conduz o loop principal do jogo: exibe o mapa e processa os comandos digitados pelo jogador */
static int executa_jogo_interativo(int *p_soquete)
{
    char entrada[64];

    printf("Conectando ao servidor...\n");

    proxima_sequencia_cliente = 0;
    if (envia_pedido_mapa(p_soquete) != 0)
        return -1;

    printf("\033[2J\033[H");
    if (recebe_mapa_completo(p_soquete) != 0)
        return -1;

    // O primeiro mapa também vem acompanhado da resposta composta do servidor.
    recebe_resposta_completa(p_soquete);

    while (1)
    {
        printf("\nw=cima  s=baixo  a=esq  d=dir  q=sair: ");
        fflush(stdout);

        if (fgets(entrada, sizeof(entrada), stdin) == NULL)
            break;

        entrada[strcspn(entrada, "\n")] = '\0';

        if (strcmp(entrada, "q") == 0 || strcmp(entrada, "quit") == 0)
            break;

        uint8_t tipo = tipo_movimento_por_texto(entrada);
        if (tipo == MSG_ERRO)
        {
            printf("Comando invalido. Use w / a / s / d.\n");
            continue;
        }

        // Cada rodada começa uma nova sequência stop-and-wait entre cliente e servidor.
        proxima_sequencia_cliente = 0;
        if (envia_movimento_pacman(p_soquete, tipo) != 0)
            return -1;

        printf("\033[2J\033[H");

        int resultado_mapa = recebe_mapa_completo(p_soquete);
        if (resultado_mapa == REDE_TIMEOUT)
        {
            printf("[AVISO] Resposta do servidor perdida. Envie o proximo movimento para sincronizar.\n");
            continue;
        }

        if (resultado_mapa != 0)
        {
            return -1;
        }

        int status = recebe_resposta_completa(p_soquete);

        if (status == REDE_TIMEOUT)
        {
            printf("[AVISO] Resposta do servidor perdida. Envie o proximo movimento para sincronizar.\n");
            continue;
        }

        if (status < 0)
        {
            return -1;
        }

        if (status == 1)
        {
            printf("\n=== VITORIA! Voce coletou todas as pastilhas! ===\n");
            break;
        }

        if (status == 2)
        {
            printf("\n=== DERROTA! O PacMan foi pego por um fantasma! ===\n");
            break;
        }
    }

    return 0;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* Interpreta a mensagem passada pela linha de comando e executa a ação correspondente no servidor */
int executa_cliente(int soquete, const char *mensagem)
{
    uint8_t tipo_movimento;

    if (mensagem == NULL || mensagem[0] == '\0')
    {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    // O prefixo "arquivo:" separa envio binário das mensagens de texto do jogo.
    if (strncmp(mensagem, "arquivo:", 8) == 0)
    {
        const char *caminho = mensagem + 8;
        uint8_t tipo = tipo_arquivo_por_caminho(caminho);

        if (tipo == MSG_ERRO)
        {
            fprintf(stderr, "[ERRO] Extensao de arquivo nao suportada: %s\n", caminho);
            return -1;
        }

        return envia_arquivo_protocolado(
            &soquete,
            caminho,
            tipo,
            &proxima_sequencia_cliente);
    }

    if (strcmp(mensagem, "jogar") == 0)
        return executa_jogo_interativo(&soquete);

    if (strcmp(mensagem, "mapa") == 0 || strcmp(mensagem, "iniciar") == 0)
    {
        if (envia_pedido_mapa(&soquete) != 0)
        {
            return -1;
        }

        if (recebe_mapa_completo(&soquete) != 0)
            return -1;

        recebe_resposta_completa(&soquete);
        return 0;
    }

    tipo_movimento = tipo_movimento_por_texto(mensagem);
    if (tipo_movimento != MSG_ERRO)
    {
        if (envia_movimento_pacman(&soquete, tipo_movimento) != 0)
        {
            return -1;
        }

        int resultado = recebe_mapa_completo(&soquete);
        if (resultado != 0)
        {
            return resultado;
        }

        recebe_resposta_completa(&soquete);
        return 0;
    }

    return envia_buffer_protocolado(
        &soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem),
        &proxima_sequencia_cliente);
}
