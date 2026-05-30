#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int envia_mensagem_protocolada(int soquete, const char *texto)
{
    mensagem_t mensagem;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    size_t tamanho_texto;

    if (texto == NULL || texto[0] == '\0')
    {
        fprintf(stderr, "[ERRO] Texto nulo ou vazio\n");
        return -1;
    }

    tamanho_texto = strlen(texto);

    /*
     * Nesta primeira etapa, ainda nao vamos fragmentar.
     * Entao a mensagem precisa caber em uma unica mensagem do protocolo.
     */
    if (tamanho_texto > TAMANHO_MAX_DADOS)
    {
        fprintf(stderr,
                "[ERRO] Mensagem grande demais para um pacote. Maximo: %d bytes\n",
                TAMANHO_MAX_DADOS);
        return -1;
    }

    memset(&mensagem, 0, sizeof(mensagem));

    mensagem.tipo_msg = MSG_DADOS;
    mensagem.num_sequencia_msg = 0;
    mensagem.tamanho_dados = (uint8_t)tamanho_texto;

    memcpy(mensagem.dados, texto, tamanho_texto);

    if (monta_pacote(&mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return -1;
    }

    printf("[DEBUG] Enviando pacote com %zu bytes\n", tamanho_pacote);

    ssize_t resultado = envia_mensagem(soquete, pacote, tamanho_pacote);

    if (resultado < 0)
    {
        if (errno == EINTR)
        {
            return envia_mensagem_protocolada(soquete, texto);
        }

        perror("envia_mensagem");
        return -1;
    }

    if ((size_t)resultado != tamanho_pacote)
    {
        fprintf(stderr,
                "[ERRO] Envio incompleto. Enviado: %zd, esperado: %zu\n",
                resultado,
                tamanho_pacote);
        return -1;
    }

    return 0;
}

int executa_cliente(int soquete, const char *mensagem)
{
    if (mensagem == NULL || mensagem[0] == '\0')
    {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    return envia_mensagem_protocolada(soquete, mensagem);
}