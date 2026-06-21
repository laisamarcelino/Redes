#ifndef LOG_H
#define LOG_H

#include "protocol.h"

/* Abre o arquivo de log e lanca um xterm com tail -f nele.
 * Retorna 0 em sucesso; o xterm e opcional (falha silenciosa). */
int  log_abre(const char *caminho);

/* Define o contexto exibido em cada linha ("SRV" ou "CLI"). */
void log_define_contexto(const char *ctx);

/* Fecha o arquivo de log. */
void log_fecha(void);

/* Registra uma mensagem de protocolo.
 * direcao: "SEND" ou "RECV" */
void log_mensagem(const char *direcao, const mensagem_t *msg);

/* Registra um evento de texto livre (aceita formato printf). */
void log_evento(const char *fmt, ...);

#endif
