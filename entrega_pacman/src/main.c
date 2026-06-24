#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "server.h"
#include "log.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exibe as opções de uso do programa no stderr e retorna ao chamador */
static void mostra_uso(const char *programa) {
    fprintf(stderr, "Uso:\n");
    fprintf(stderr, "  %s -s [-m mapa.csv] [-v] [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c mapa [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c cima|baixo|esquerda|direita [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c \"mensagem\" [-l] [-i interface]\n", programa);
    fprintf(stderr, "  %s -c \"arquivo:caminho/do/arquivo.txt\" [-l] [-i interface]\n", programa);
    fprintf(stderr, "\nOpcoes:\n");
    fprintf(stderr, "  -c  modo cliente, enviando a mensagem informada\n");
    fprintf(stderr, "  -s  modo servidor\n");
    fprintf(stderr, "  -m  caminho do mapa CSV (padrao: maps/padrao_ufpr.csv)\n");
    fprintf(stderr, "  -v  exibe log de mensagens enviadas e recebidas\n");
    fprintf(stderr, "  -l  permite loopback para testes locais\n");
    fprintf(stderr, "  -i  escolhe manualmente a interface de rede. Ex: enp3s0, eth0, lo\n");
    fprintf(stderr, "\nArquivos suportados: .txt, .jpg, .jpeg e .mp4\n");
}

int main(int argc, char **argv) {
    int modo_cliente = 0;
    int modo_servidor = 0;
    int permite_loopback = 0;
    int verbose = 0;
    char *mensagem_cliente = NULL;
    char *interface_manual = NULL;
    char *caminho_mapa = NULL;

    /* Lê cada opção passada na linha de comando e armazena os valores correspondentes */
    int opcao;
    while ((opcao = getopt(argc, argv, "c:si:lhm:v")) != -1) {
        switch (opcao) {
            case 'c':
                modo_cliente = 1;
                mensagem_cliente = optarg;
                break;
            case 's':
                modo_servidor = 1;
                break;
            case 'm':
                caminho_mapa = optarg;
                break;
            case 'v':
                verbose = 1;
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

    /* Ativa o log de mensagens de rede se o usuário pediu modo verboso */
    if (verbose) {
        log_define_ativo(1);
        log_define_contexto(modo_servidor ? "SRV" : "CLI");
    }

    /* Garante que exatamente um modo (cliente ou servidor) foi escolhido */
    if (modo_cliente == modo_servidor) {
        mostra_uso(argv[0]);
        return 1;
    }

    if (modo_cliente && mensagem_cliente == NULL) {
        mostra_uso(argv[0]);
        return 1;
    }

    /* Define a interface de rede a ser usada: manual (-i) ou selecionada automaticamente */
    // A opção -i foi mantida para evitar seleção automática de interfaces virtuais em testes.
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

    /* Abre o socket de comunicação de rede e libera a string da interface */
    int soquete = cria_raw_socket(interface_rede);
    free(interface_rede);
    if (soquete < 0) {
        return 1;
    }

    /* Executa o modo escolhido e fecha o socket ao terminar */
    int resultado;
    if (modo_cliente) {
        resultado = executa_cliente(soquete, mensagem_cliente);
    } else {
        resultado = executa_servidor(soquete, caminho_mapa);
    }

    fecha_raw_socket(soquete);
    return resultado == 0 ? 0 : 1;
}
