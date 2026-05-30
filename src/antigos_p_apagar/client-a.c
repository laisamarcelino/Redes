#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ACKs temporariamente desativados: não aguardamos confirmação do servidor. */

// Envia a mensagem inteira de uma vez, sem cabeçalho de protocolo e sem fragmentação.
static int envia_mensagem_completa(int soquete, const char *mensagem) {
    size_t len = strlen(mensagem);
    int termina_com_nova_linha = (len > 0 && mensagem[len - 1] == '\n');

    size_t enviar_len = len + (termina_com_nova_linha ? 0 : 1);
    unsigned char *buf = malloc(enviar_len);
    if (buf == NULL) {
        fprintf(stderr, "Erro ao alocar buffer de envio\n");
        return -1;
    }

    memcpy(buf, mensagem, len);
    if (!termina_com_nova_linha) {
        buf[len] = '\n';
    }

    ssize_t resultado = envia_mensagem(soquete, buf, enviar_len);
    free(buf);

    if (resultado < 0) {
        if (errno == EINTR) {
            return envia_mensagem_completa(soquete, mensagem);
        }
        perror("envia_mensagem");
        return -1;
    }

    if ((size_t) resultado != enviar_len) {
        fprintf(stderr, "Erro ao enviar mensagem completa\n");
        return -1;
    }

    return 0;
}

// Envia a mensagem recebida pela linha de comando.
int executa_cliente(int soquete, const char *mensagem) {
    if (mensagem == NULL || mensagem[0] == '\0') {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    return envia_mensagem_completa(soquete, mensagem);
}
