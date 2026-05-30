#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void imprime_mensagem_protocolada(const mensagem_t *mensagem)
{
    if (mensagem == NULL)
    {
        return;
    }

    printf("[DEBUG] Tipo: %u\n", mensagem->tipo_msg);
    printf("[DEBUG] Sequencia: %u\n", mensagem->num_sequencia_msg);
    printf("[DEBUG] Tamanho dados: %u\n", mensagem->tamanho_dados);

    printf("[DEBUG] Dados: ");
    for (uint8_t i = 0; i < mensagem->tamanho_dados; i++)
    {
        unsigned char c = mensagem->dados[i];

        if (c >= 32 && c <= 126)
        {
            putchar(c);
        }
        else
        {
            printf("\\x%02X", c);
        }
    }
    putchar('\n');
    fflush(stdout);
}

int executa_servidor(int soquete)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;

    printf("Servidor aguardando pacotes do protocolo PacMan...\n");

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        printf("\n[DEBUG] Pacote recebido com %zd bytes\n", recebido);

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido\n");
            continue;
        }

        imprime_mensagem_protocolada(&mensagem);
    }
}