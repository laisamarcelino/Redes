#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int  log_ativo    = 0;
static char log_contexto[8] = "SRV";
static FILE *log_arquivo = NULL;
static char log_caminho[64] = "";

/* Fecha o arquivo de log aberto pelo processo atual */
static void fecha_arquivo_log(void)
{
    if (log_arquivo != NULL)
    {
        fclose(log_arquivo);
        log_arquivo = NULL;
    }
}

/* Abre sob demanda o arquivo /tmp/pacman_<ctx>.log correspondente ao contexto atual */
static FILE *obtem_destino_log(void)
{
    char caminho[64];

    if (!log_ativo)
        return NULL;

    snprintf(caminho, sizeof(caminho), "/tmp/pacman_%s.log", log_contexto);

    if (log_arquivo != NULL && strcmp(log_caminho, caminho) == 0)
        return log_arquivo;

    fecha_arquivo_log();

    log_arquivo = fopen(caminho, "a");
    if (log_arquivo == NULL)
    {
        fprintf(stderr, "[ERRO] Falha ao abrir arquivo de log %s: %s\n",
                caminho, strerror(errno));
        return NULL;
    }

    strncpy(log_caminho, caminho, sizeof(log_caminho) - 1);
    log_caminho[sizeof(log_caminho) - 1] = '\0';

    setvbuf(log_arquivo, NULL, _IOLBF, 0);
    return log_arquivo;
}

/* Ativa ou desativa a exibição de logs durante a execução */
void log_define_ativo(int ativo)
{
    log_ativo = ativo;

    if (!log_ativo)
    {
        fecha_arquivo_log();
        log_caminho[0] = '\0';
    }
}

/* Define o rótulo do processo atual (ex: "SRV" ou "CLI") para identificar as mensagens de log */
void log_define_contexto(const char *ctx)
{
    if (ctx == NULL) return;
    int i;
    for (i = 0; i < 7 && ctx[i]; i++) log_contexto[i] = ctx[i];
    log_contexto[i] = '\0';

    if (log_arquivo != NULL)
    {
        fecha_arquivo_log();
        log_caminho[0] = '\0';
    }
}

/* Retorna o rótulo de contexto atual do processo */
const char *log_get_contexto(void)
{
    return log_contexto;
}

/* Converte um tipo de mensagem numérico no seu nome legível para exibição no log */
static const char *nome_tipo(uint8_t tipo)
{
    switch (tipo)
    {
    case MSG_ACK:             return "ACK";
    case MSG_NACK:            return "NACK";
    case MSG_VISUALIZACAO:    return "VISUALIZACAO";
    case MSG_INICIALIZACAO:   return "INICIALIZACAO";
    case MSG_DADOS:           return "DADOS";
    case MSG_TXT:             return "TXT";
    case MSG_JPG:             return "JPG";
    case MSG_MP4:             return "MP4";
    case MSG_MOV_CIMA:        return "MOV_CIMA";
    case MSG_MOV_BAIXO:       return "MOV_BAIXO";
    case MSG_MOV_ESQUERDA:    return "MOV_ESQUERDA";
    case MSG_MOV_DIREITA:     return "MOV_DIREITA";
    case MSG_FIM_JOGO:        return "FIM_JOGO";
    case MSG_ERRO:            return "ERRO";
    case MSG_FIM_TRANSMISSAO: return "FIM_TRANSMISSAO";
    default:                  return "DESCONHECIDO";
    }
}

/* Imprime o horário atual como prefixo de uma linha de log */
static void escreve_prefixo(void)
{
    time_t agora = time(NULL);
    struct tm *tm_info = localtime(&agora);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    FILE *destino = obtem_destino_log();

    if (destino == NULL)
        return;

    fprintf(destino, "[%s] ", buf);
}

/* Registra no terminal os detalhes de uma mensagem de protocolo, se o log estiver ativo */
void log_mensagem(const char *direcao, const mensagem_t *msg)
{
    FILE *destino;

    if (!log_ativo || msg == NULL)
        return;

    destino = obtem_destino_log();
    if (destino == NULL)
        return;

    escreve_prefixo();
    fprintf(destino, "%-10s  %-16s  seq=%02u  %u bytes\n",
           direcao,
           nome_tipo(msg->tipo_msg),
           msg->num_sequencia_msg,
           msg->tamanho_dados);
    fflush(destino);
}

/* Registra no terminal uma linha de evento com formato livre, se o log estiver ativo */
void log_evento(const char *fmt, ...)
{
    va_list args;
    FILE *destino;

    if (!log_ativo)
        return;

    destino = obtem_destino_log();
    if (destino == NULL)
        return;

    escreve_prefixo();
    va_start(args, fmt);
    vfprintf(destino, fmt, args);
    va_end(args);
    fputc('\n', destino);
    fflush(destino);
}
