#include "include/network.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_TAMANHO 65536
#define TIMEOUT_MS 5000

int main(void) {
    char interface[256] = {0};
    int soquete = -1;
    unsigned char *buffer_recv = NULL;
    struct sockaddr_ll endereco_cliente = {0};
    int contador_mensagens = 0;

    printf("===== Servidor - Aguardando mensagens/arquivos =====\n");

    /* Aloca buffer para receber dados. */
    buffer_recv = malloc(BUFFER_TAMANHO);
    if (buffer_recv == NULL) {
        fprintf(stderr, "Erro: Falha ao alocar memoria\n");
        return -1;
    }

    /* Escolhe interface de rede. */
    if (escolhe_interface_disponivel(interface, sizeof(interface)) == -1) {
        fprintf(stderr, "Erro: Nenhuma interface de rede disponivel\n");
        free(buffer_recv);
        return -1;
    }

    printf("Interface selecionada: %s\n", interface);

    /* Cria socket raw. */
    soquete = cria_raw_socket(interface);
    if (soquete == -1) {
        fprintf(stderr, "Erro: Falha ao criar socket\n");
        free(buffer_recv);
        return -1;
    }

    printf("Socket criado com sucesso\n");

    /* Configura timeout para recebimento. */
    if (configura_timeout_socket(soquete, TIMEOUT_MS) == -1) {
        fprintf(stderr, "Erro: Falha ao configurar timeout\n");
        fecha_raw_socket(soquete);
        free(buffer_recv);
        return -1;
    }

    printf("Aguardando mensagens (timeout: %d ms)...\n\n", TIMEOUT_MS);

    /* Loop de recebimento de mensagens/arquivos. */
    while (1) {
        memset(buffer_recv, 0, BUFFER_TAMANHO);
        memset(&endereco_cliente, 0, sizeof(endereco_cliente));

        /* Recebe dados do cliente. */
        ssize_t tamanho_recebido = recebe_mensagem(soquete, buffer_recv,
                                                   BUFFER_TAMANHO - 1,
                                                   &endereco_cliente);

        if (tamanho_recebido == -1) {
            /* Timeout ou erro. */
            continue;
        }

        if (tamanho_recebido < 1) {
            continue;
        }

        contador_mensagens++;

        /* Dados recebidos com sucesso. */
        printf("===== Mensagem #%d =====\n", contador_mensagens);
        printf("Tamanho: %ld bytes\n", tamanho_recebido);
        printf("MAC origem: ");
        for (int i = 0; i < 6; i++) {
            if (i > 0) printf(":");
            printf("%02x", endereco_cliente.sll_addr[i]);
        }
        printf("\n");

        unsigned char tipo = buffer_recv[0];
        unsigned char *conteudo = buffer_recv + 1;
        ssize_t tamanho_conteudo = tamanho_recebido - 1;

        if (tipo == 'T') {
            printf("Mensagem: %.*s\n", (int)tamanho_conteudo, (char *)conteudo);
        } else if (tipo == 'A') {
            /* Salva arquivo recebido. */
            char nome_arquivo[256] = {0};
            snprintf(nome_arquivo, sizeof(nome_arquivo),
                    "recebido_%d_%ld_bytes.bin",
                    contador_mensagens, tamanho_conteudo);

            FILE *arquivo = fopen(nome_arquivo, "wb");
            if (arquivo != NULL) {
                fwrite(conteudo, 1, tamanho_conteudo, arquivo);
                fclose(arquivo);
                printf("Arquivo salvo: %s\n", nome_arquivo);
            } else {
                fprintf(stderr, "Erro: Falha ao salvar arquivo\n");
            }
        } else {
            fprintf(stderr, "Erro: Tipo de mensagem invalido\n");
        }

        /* Envia confirmação ao cliente. */
        const char *resposta = "OK";
        ssize_t enviados = envia_mensagem(soquete, (void *)resposta,
                                         strlen(resposta),
                                         &endereco_cliente);

        if (enviados == -1) {
            fprintf(stderr, "Erro: Falha ao enviar confirmacao\n");
        } else {
            printf("Confirmacao enviada: %ld bytes\n", enviados);
        }

        printf("---\n\n");
    }

    fecha_raw_socket(soquete);
    free(buffer_recv);
    return 0;
}
