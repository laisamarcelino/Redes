#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "server.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mostra a forma correta de uso do programa.
static void mostra_uso(const char *programa) {
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  %s -s [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c \"mensagem\" [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c \"arquivo:caminho/do/arquivo.txt\" [-l] [-i interface]\n", programa);
    fprintf(stderr, "\nOpcoes:\n");
    fprintf(stderr, "  -c  modo cliente, enviando a mensagem informada\n");
    fprintf(stderr, "  -s  modo servidor\n");
    fprintf(stderr, "  -l  permite loopback para testes locais\n");
    fprintf(stderr, "  -i  escolhe manualmente a interface de rede. Ex: enp3s0, eth0, lo\n");
    fprintf(stderr, "\nArquivos suportados: .txt, .jpg, .jpeg e .mp4\n");
}

// Seleciona o modo cliente ou servidor e abre o socket na interface correta.
int main(int argc, char **argv) {
    int modo_cliente = 0;
    int modo_servidor = 0;
    int permite_loopback = 0;
    char *mensagem_cliente = NULL;
    char *interface_manual = NULL;

    int opcao;
    /*
     * * CORRECAO: adiciona -i para escolher a interface manualmente.
     * Isso evita selecionar Docker, bridge, VPN ou outra interface errada.
     */
    while ((opcao = getopt(argc, argv, "c:si:lh")) != -1) {
        switch (opcao) {
            case 'c':
                modo_cliente = 1;
                mensagem_cliente = optarg;
                break;
            case 's':
                modo_servidor = 1;
                break;
            case 'i':
                interface_manual = optarg;
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
    // * Usa a interface passada por -i ou escolhe automaticamente.
    char *interface_rede = NULL;
    if (interface_manual != NULL) {
        interface_rede = malloc(strlen(interface_manual) + 1);
        if (interface_rede == NULL) {
            fprintf(stderr, "Erro ao alocar nome da interface\n");
            return 1;
        }
        strcpy(interface_rede, interface_manual);
        printf("Interface de rede informada manualmente: %s\n", interface_rede);
    } else {
        interface_rede = seleciona_interface_rede(permite_loopback);
        if (interface_rede == NULL) {
            return 1;
        }
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
