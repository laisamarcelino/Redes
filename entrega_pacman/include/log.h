#ifndef LOG_H
#define LOG_H

#include "protocol.h"

/* Ativa ou desativa a saida de log no stdout (0 = off, 1 = on). */
void log_define_ativo(int ativo);

/* Define o contexto exibido em cada linha ("SRV" ou "CLI"). */
void log_define_contexto(const char *ctx);

/* Retorna "SRV" ou "CLI" conforme o contexto atual. */
const char *log_get_contexto(void);

/* Registra uma mensagem de protocolo.
 * direcao: "SEND" ou "RECV" */
void log_mensagem(const char *direcao, const mensagem_t *msg);

/* Registra um evento de texto livre (aceita formato printf). */
void log_evento(const char *fmt, ...);

#endif
