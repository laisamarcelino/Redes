#define _POSIX_C_SOURCE 200809L

#include "../include/server.h"
#include "../include/network.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TAM_BUFFER_ESCUTA 2048

// Imprime a mensagem recebida e garante quebra de linha no final.
static void imprime_mensagem_recebida(const unsigned char *buffer, size_t tamanho) {
    fwrite(buffer, 1, tamanho, stdout);
    if (tamanho == 0 || buffer[tamanho - 1] != '\n') {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

// Fica esperando mensagens do cliente e imprime quando completar uma linha.
int executa_servidor(int soquete) {
    unsigned char buffer[TAM_BUFFER_ESCUTA];
    unsigned char *mensagem = NULL;
    size_t tamanho_mensagem = 0;
    size_t capacidade_mensagem = 0;

    printf("Servidor aguardando mensagens do protocolo PacMan...\n");

    while (1) {
        ssize_t recebido = espera_mensagem_servidor(soquete, buffer, sizeof(buffer));
        if (recebido < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("espera_mensagem_servidor");
            free(mensagem);
            return -1;
        }

        if (recebido == 0) {
            continue;
        }

        /*
         * * CORRECAO: o filtro por texto legível foi removido.
         * Agora network.c já entrega apenas quadros com o EtherType do projeto.
         */
        if (tamanho_mensagem + (size_t) recebido > capacidade_mensagem) {
            // * Cresce o buffer quando a mensagem não couber mais.
            size_t nova_capacidade = capacidade_mensagem == 0 ? 1024 : capacidade_mensagem;
            while (tamanho_mensagem + (size_t) recebido > nova_capacidade) {
                nova_capacidade *= 2;
            }

            unsigned char *nova_mensagem = realloc(mensagem, nova_capacidade);
            if (nova_mensagem == NULL) {
                free(mensagem);
                fprintf(stderr, "Erro ao alocar buffer da mensagem\n");
                return -1;
            }

            mensagem = nova_mensagem;
            capacidade_mensagem = nova_capacidade;
        }

        memcpy(mensagem + tamanho_mensagem, buffer, (size_t) recebido);
        tamanho_mensagem += (size_t) recebido;

        // Quando achar '\n', a mensagem completa já chegou.
        unsigned char *fim_linha = memchr(mensagem, '\n', tamanho_mensagem);
        if (fim_linha != NULL) {
            size_t tamanho_para_imprimir = (size_t) (fim_linha - mensagem) + 1;
            printf("Mensagem recebida: ");
            imprime_mensagem_recebida(mensagem, tamanho_para_imprimir);
            free(mensagem);
            mensagem = NULL;
            tamanho_mensagem = 0;
            capacidade_mensagem = 0;
        }
    }
}
