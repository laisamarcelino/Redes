#include "include/network.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_TAMANHO 65536
#define TIMEOUT_MS 5000

/* MAC broadcast (todos os 6 bytes = 0xFF). */
static unsigned char mac_broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

int main(int argc, char *argv[]) {
    char interface[256] = {0};
    int soquete = -1;
    int ifindex = -1;
    unsigned char mac_local[6] = {0};
    unsigned char *buffer_envio = NULL;
    unsigned char buffer_recv[BUFFER_TAMANHO] = {0};
    struct sockaddr_ll endereco_destino = {0};
    struct sockaddr_ll endereco_origem = {0};
    size_t tamanho_envio = 0;
    int modo_forcado = 0; /* 1: texto, 2: arquivo */

    printf("===== Cliente - Enviando mensagens/arquivos =====\n");

    if (argc < 3) {
        fprintf(stderr, "Uso: %s -t <mensagem>\n", argv[0]);
        fprintf(stderr, "Uso: %s -a <arquivo>\n", argv[0]);
        fprintf(stderr, "\nExemplo (texto): %s -t \"Olá servidor\"\n", argv[0]);
        fprintf(stderr, "Exemplo (arquivo): %s -a dados.bin\n", argv[0]);
        return -1;
    }

    const char *argumento = NULL;

    /* Processa opções. */
    if (strcmp(argv[1], "-t") == 0) {
        modo_forcado = 1;
        argumento = argv[2];
    } else if (strcmp(argv[1], "-a") == 0) {
        modo_forcado = 2;
        argumento = argv[2];
    } else {
        fprintf(stderr, "Erro: Opção desconhecida: %s\n", argv[1]);
        return -1;
    }

    /* Modo texto. */
    if (modo_forcado == 1) {
        printf("Modo: Texto\n");
        size_t tamanho_texto = strlen(argumento);
        if (tamanho_texto + 1 > MENSAGEM_PAYLOAD_MAX) {
            fprintf(stderr, "Erro: Mensagem muito grande. Limite: %zu bytes\n",
                    (size_t)MENSAGEM_PAYLOAD_MAX - 1);
            return -1;
        }

        buffer_envio = malloc(tamanho_texto + 1);
        if (buffer_envio == NULL) {
            fprintf(stderr, "Erro: Falha ao alocar memoria\n");
            return -1;
        }

        buffer_envio[0] = 'T';
        memcpy(buffer_envio + 1, argumento, tamanho_texto);
        tamanho_envio = tamanho_texto + 1;
        printf("Mensagem: %s\n", argumento);
    }
    /* Modo arquivo. */
    else if (modo_forcado == 2) {
        printf("Modo: Arquivo\n");
        printf("Arquivo: %s\n", argumento);

        FILE *arquivo = fopen(argumento, "rb");
        if (arquivo == NULL) {
            fprintf(stderr, "Erro: Falha ao abrir arquivo\n");
            return -1;
        }

        buffer_envio = malloc(MENSAGEM_PAYLOAD_MAX);
        if (buffer_envio == NULL) {
            fprintf(stderr, "Erro: Falha ao alocar memoria\n");
            fclose(arquivo);
            return -1;
        }

        /* Lê arquivo. */
        buffer_envio[0] = 'A';
        size_t lidos = fread(buffer_envio + 1, 1, MENSAGEM_PAYLOAD_MAX - 1, arquivo);
        if (ferror(arquivo) != 0) {
            fprintf(stderr, "Erro: Falha na leitura do arquivo\n");
            free(buffer_envio);
            fclose(arquivo);
            return -1;
        }

        if (lidos == 0) {
            fprintf(stderr, "Erro: Arquivo vazio\n");
            free(buffer_envio);
            fclose(arquivo);
            return -1;
        }

        if (fgetc(arquivo) != EOF) {
            fprintf(stderr, "Erro: Arquivo excede o limite de %zu bytes por envio\n",
                    (size_t)MENSAGEM_PAYLOAD_MAX - 1);
            free(buffer_envio);
            fclose(arquivo);
            return -1;
        }

        fclose(arquivo);
        tamanho_envio = lidos + 1;
        printf("Tamanho do arquivo: %zu bytes\n", lidos);
    }

    /* Escolhe interface de rede. */
    if (escolhe_interface_disponivel(interface, sizeof(interface)) == -1) {
        fprintf(stderr, "Erro: Nenhuma interface de rede disponivel\n");
        free(buffer_envio);
        return -1;
    }

    printf("Interface selecionada: %s\n", interface);

    /* Obtém índice da interface. */
    ifindex = obtem_ifindex_interface(interface);
    if (ifindex == -1) {
        fprintf(stderr, "Erro: Falha ao obter indice da interface\n");
        free(buffer_envio);
        return -1;
    }

    /* Obtém MAC local. */
    if (obtem_mac_interface(interface, mac_local) == -1) {
        fprintf(stderr, "Erro: Falha ao obter MAC da interface\n");
        free(buffer_envio);
        return -1;
    }

    printf("MAC local: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) printf(":");
        printf("%02x", mac_local[i]);
    }
    printf("\n");

    /* Cria socket raw. */
    soquete = cria_raw_socket(interface);
    if (soquete == -1) {
        fprintf(stderr, "Erro: Falha ao criar socket\n");
        free(buffer_envio);
        return -1;
    }

    printf("Socket criado com sucesso\n");

    /* Configura timeout para recebimento. */
    if (configura_timeout_socket(soquete, TIMEOUT_MS) == -1) {
        fprintf(stderr, "Erro: Falha ao configurar timeout\n");
        fecha_raw_socket(soquete);
        free(buffer_envio);
        return -1;
    }

    /* Configura endereço de destino (broadcast). */
    if (configura_endereco_destino_raw(ifindex, mac_broadcast,
                                       &endereco_destino) == -1) {
        fprintf(stderr, "Erro: Falha ao configurar endereco destino\n");
        fecha_raw_socket(soquete);
        free(buffer_envio);
        return -1;
    }

    printf("Endereco destino (broadcast) configurado\n");

    /* Envia dados. */
    printf("\nEnviando %zu bytes...\n", tamanho_envio);
    ssize_t enviados = envia_mensagem(soquete, (void *)buffer_envio,
                                     tamanho_envio,
                                     &endereco_destino);

    if (enviados == -1) {
        fprintf(stderr, "Erro: Falha ao enviar\n");
        fecha_raw_socket(soquete);
        free(buffer_envio);
        return -1;
    }

    printf("Dados enviados: %ld bytes\n\n", enviados);

    /* Aguarda resposta do servidor. */
    printf("Aguardando resposta do servidor (timeout: %d ms)...\n", TIMEOUT_MS);

    memset(buffer_recv, 0, sizeof(buffer_recv));
    memset(&endereco_origem, 0, sizeof(endereco_origem));

    ssize_t recebido = recebe_mensagem(soquete, buffer_recv,
                                      sizeof(buffer_recv) - 1,
                                      &endereco_origem);

    if (recebido == -1) {
        printf("Timeout: Sem resposta\n");
    } else {
        printf("Resposta: %s\n", (char *)buffer_recv);
    }

    fecha_raw_socket(soquete);
    free(buffer_envio);
    return 0;
}
