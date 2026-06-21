#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static int  log_ativo    = 0;
static char log_contexto[8] = "SRV";

void log_define_ativo(int ativo)
{
    log_ativo = ativo;
}

void log_define_contexto(const char *ctx)
{
    if (ctx == NULL) return;
    int i;
    for (i = 0; i < 7 && ctx[i]; i++) log_contexto[i] = ctx[i];
    log_contexto[i] = '\0';
}

const char *log_get_contexto(void)
{
    return log_contexto;
}

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

static void escreve_prefixo(void)
{
    time_t agora = time(NULL);
    struct tm *tm_info = localtime(&agora);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    printf("[%s] ", buf);
}

void log_mensagem(const char *direcao, const mensagem_t *msg)
{
    if (!log_ativo || msg == NULL)
        return;

    escreve_prefixo();
    printf("%-10s  %-16s  seq=%02u  %u bytes\n",
           direcao,
           nome_tipo(msg->tipo_msg),
           msg->num_sequencia_msg,
           msg->tamanho_dados);
    fflush(stdout);
}

void log_evento(const char *fmt, ...)
{
    va_list args;

    if (!log_ativo)
        return;

    escreve_prefixo();
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}
