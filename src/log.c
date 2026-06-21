#define _POSIX_C_SOURCE 200809L

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

static FILE *log_arquivo = NULL;
static char  log_contexto[8] = "---";

void log_define_contexto(const char *ctx)
{
    if (ctx == NULL) return;
    int i;
    for (i = 0; i < 7 && ctx[i]; i++) log_contexto[i] = ctx[i];
    log_contexto[i] = '\0';
}

static void escreve_timestamp(void)
{
    time_t agora = time(NULL);
    struct tm *tm_info = localtime(&agora);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    fprintf(log_arquivo, "[%s] ", buf);
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

int log_abre(const char *caminho)
{
    log_arquivo = fopen(caminho, "w");
    if (log_arquivo == NULL)
    {
        perror("[ERRO] log_abre");
        return -1;
    }

    /* Buffer de linha para que tail -f veja cada linha assim que e escrita */
    setvbuf(log_arquivo, NULL, _IOLBF, 0);

    /* Abre xterm com tail -f em background; falha silenciosa se nao existir */
    pid_t pid = fork();
    if (pid == 0)
    {
        execlp("xterm", "xterm", "-title", "PacMan - Log",
               "-e", "tail", "-f", caminho, (char *)NULL);
        _exit(1);
    }

    escreve_timestamp();
    fprintf(log_arquivo, "=== Log PacMan iniciado ===\n");
    return 0;
}

void log_fecha(void)
{
    if (log_arquivo == NULL)
        return;

    escreve_timestamp();
    fprintf(log_arquivo, "=== Log PacMan encerrado ===\n");
    fclose(log_arquivo);
    log_arquivo = NULL;
}

void log_mensagem(const char *direcao, const mensagem_t *msg)
{
    if (log_arquivo == NULL || msg == NULL)
        return;

    escreve_timestamp();
    fprintf(log_arquivo, "[%s] %s  %-16s  seq=%02u  %u bytes\n",
            log_contexto,
            direcao,
            nome_tipo(msg->tipo_msg),
            msg->num_sequencia_msg,
            msg->tamanho_dados);
}

void log_evento(const char *fmt, ...)
{
    va_list args;

    if (log_arquivo == NULL)
        return;

    escreve_timestamp();
    fprintf(log_arquivo, "[%s] ", log_contexto);
    va_start(args, fmt);
    vfprintf(log_arquivo, fmt, args);
    va_end(args);
    fputc('\n', log_arquivo);
}
