#define _POSIX_C_SOURCE 200809L

#include "../include/client.h"
#include "../include/network.h"
#include "../include/server.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>

// Mostra a forma correta de uso do programa.
static void mostra_uso(const char *programa) {
    fprintf(stderr, "Uso: %s -c \"mensagem\" | -s [-l]\n", programa);
    fprintf(stderr, "  -c  modo cliente\n");
    fprintf(stderr, "  -s  modo servidor\n");
    fprintf(stderr, "  -l  permite loopback para testes\n");
}

// Seleciona o modo cliente ou servidor e abre o socket na interface correta.
int main(int argc, char **argv) {
    int modo_cliente = 0;
    int modo_servidor = 0;
    int permite_loopback = 0;
    char *mensagem_cliente = NULL;

    int opcao;
    /* -c aceita um argumento (mensagem). Permite que -l seja passado em
       qualquer posição porque getopt consome o argumento de -c automaticamente. */
    while ((opcao = getopt(argc, argv, "c:slh")) != -1) {
        switch (opcao) {
            case 'c':
                modo_cliente = 1;
                mensagem_cliente = optarg;
                break;
            case 's':
                modo_servidor = 1;
                break;
            case 'l':
                permite_loopback = 1;
                break;
            case 'h':
            default:
                mostra_uso(argv[0]);
                return 1;
        }
    }

    if (modo_cliente == modo_servidor) {
        mostra_uso(argv[0]);
        return 1;
    }

    if (modo_cliente && mensagem_cliente == NULL) {
        mostra_uso(argv[0]);
        return 1;
    }

    // Escolhe a interface de rede e abre o raw socket nela.
    char *interface_rede = seleciona_interface_rede(permite_loopback);
    if (interface_rede == NULL) {
        return 1;
    }

    int soquete = cria_raw_socket(interface_rede);
    free(interface_rede);
    if (soquete < 0) {
        return 1;
    }

    // Executa cliente ou servidor conforme a opção escolhida.
    int resultado;
    if (modo_cliente) {
        resultado = executa_cliente(soquete, mensagem_cliente);
    } else {
        resultado = executa_servidor(soquete);
    }

    fecha_raw_socket(soquete);
    return resultado == 0 ? 0 : 1;
}
