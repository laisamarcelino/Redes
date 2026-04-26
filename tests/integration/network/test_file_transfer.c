#include "include/network.h"

#include <stdint.h>
#include <string.h>

#include <linux/if_ether.h>

#define TEST_ETHER_TYPE 0x88B6
#define TEST_MAGIC 0x46545231u
#define TEST_MAX_FILE_SIZE 512
#define TEST_FRAME_BUFFER 2048
#define TEST_INPUT_FILE_PATH "tests/fixtures/arquivo_envio.txt"

struct test_header {
    uint32_t magic;
    uint16_t tamanho;
} __attribute__((packed));

static int le_arquivo_teste_preexistente(unsigned char *dados, size_t *tamanho_dados) {
    if (dados == NULL || tamanho_dados == NULL) {
        return -1;
    }

    FILE *arquivo = fopen(TEST_INPUT_FILE_PATH, "rb");
    if (arquivo == NULL) {
        return -1;
    }

    size_t lidos = fread(dados, 1, TEST_MAX_FILE_SIZE, arquivo);
    if (ferror(arquivo) != 0) {
        fclose(arquivo);
        return -1;
    }

    if (!feof(arquivo)) {
        fclose(arquivo);
        return -1;
    }

    fclose(arquivo);
    *tamanho_dados = lidos;
    return 0;
}


int main(void) {
    char interface[IF_NAMESIZE] = {0};
    unsigned char payload[TEST_MAX_FILE_SIZE] = {0};
    size_t lidos = 0;

    if (geteuid() != 0) {
        printf("SKIP: teste de transferencia exige root ou CAP_NET_RAW\n");
        return 0;
    }

    if (escolhe_interface_disponivel(interface, sizeof(interface)) != 0) {
        fprintf(stderr, "Falha ao localizar interface de rede\n");
        return 1;
    }

    if (le_arquivo_teste_preexistente(payload, &lidos) != 0) {
        fprintf(stderr, "Falha ao ler arquivo pre-existente de teste: %s\n", TEST_INPUT_FILE_PATH);
        return 1;
    }

    if (lidos == 0) {
        fprintf(stderr, "Arquivo de teste vazio: %s\n", TEST_INPUT_FILE_PATH);
        return 1;
    }

    int soquete_rx = cria_raw_socket(interface);
    int soquete_tx = cria_raw_socket(interface);

    if (configura_timeout_socket(soquete_rx, 500) != 0) {
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    if (configura_timeout_socket(soquete_tx, 500) != 0) {
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    int ifindex = obtem_ifindex_interface(interface);
    if (ifindex <= 0) {
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    unsigned char mac_interface[ETH_ALEN] = {0};
    if (obtem_mac_interface(interface, mac_interface) != 0) {
        fprintf(stderr, "Falha ao obter MAC da interface\n");
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    unsigned char mac_destino[ETH_ALEN] = {0};
    memcpy(mac_destino, mac_interface, ETH_ALEN);

    struct sockaddr_ll endereco_destino = {0};
    if (configura_endereco_destino_raw(ifindex, mac_destino, &endereco_destino) != 0) {
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    unsigned char frame_envio[sizeof(struct ethhdr) + sizeof(struct test_header) + TEST_MAX_FILE_SIZE] = {0};
    struct ethhdr *eth = (struct ethhdr *)frame_envio;
    memcpy(eth->h_dest, mac_destino, ETH_ALEN);
    memcpy(eth->h_source, mac_interface, ETH_ALEN);
    eth->h_proto = htons(TEST_ETHER_TYPE);

    struct test_header *cab = (struct test_header *)(frame_envio + sizeof(struct ethhdr));
    cab->magic = htonl(TEST_MAGIC);
    cab->tamanho = htons((uint16_t)lidos);

    memcpy(frame_envio + sizeof(struct ethhdr) + sizeof(struct test_header), payload, lidos);
    size_t tamanho_frame = sizeof(struct ethhdr) + sizeof(struct test_header) + lidos;

    ssize_t enviados = envia_bytes_raw(soquete_tx, frame_envio, tamanho_frame, &endereco_destino);
    if (enviados != (ssize_t)tamanho_frame) {
        fprintf(stderr, "Falha ao enviar frame de arquivo\n");
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    unsigned char buffer_rx[TEST_FRAME_BUFFER] = {0};
    unsigned char payload_recebido[TEST_MAX_FILE_SIZE + 1] = {0};
    size_t tamanho_recebido = 0;
    int recebeu = 0;

    for (int tentativa = 0; tentativa < 30; tentativa++) {
        struct sockaddr_ll origem = {0};
        ssize_t recebidos = recebe_bytes_raw(soquete_rx, buffer_rx, sizeof(buffer_rx), &origem);
        if (recebidos <= 0) {
            continue;
        }

        if ((size_t)recebidos < sizeof(struct ethhdr) + sizeof(struct test_header)) {
            continue;
        }

        struct ethhdr *eth_rx = (struct ethhdr *)buffer_rx;
        if (ntohs(eth_rx->h_proto) != TEST_ETHER_TYPE) {
            continue;
        }

        struct test_header *cab_rx = (struct test_header *)(buffer_rx + sizeof(struct ethhdr));
        if (ntohl(cab_rx->magic) != TEST_MAGIC) {
            continue;
        }

        size_t tamanho_payload = ntohs(cab_rx->tamanho);
        size_t disponivel = (size_t)recebidos - sizeof(struct ethhdr) - sizeof(struct test_header);
        if (tamanho_payload != lidos || tamanho_payload > disponivel) {
            continue;
        }

        if (memcmp(buffer_rx + sizeof(struct ethhdr) + sizeof(struct test_header), payload, lidos) != 0) {
            fprintf(stderr, "Payload recebido difere do arquivo enviado\n");
            fecha_raw_socket(soquete_rx);
            fecha_raw_socket(soquete_tx);
            return 1;
        }

        memcpy(payload_recebido,
               buffer_rx + sizeof(struct ethhdr) + sizeof(struct test_header),
               tamanho_payload);
        payload_recebido[tamanho_payload] = '\0';
        tamanho_recebido = tamanho_payload;

        recebeu = 1;
        break;
    }

    if (!recebeu) {
        fprintf(stderr, "Falha ao receber o arquivo enviado via raw socket\n");
        fecha_raw_socket(soquete_rx);
        fecha_raw_socket(soquete_tx);
        return 1;
    }

    if (fecha_raw_socket(soquete_tx) != 0) {
        fecha_raw_socket(soquete_rx);
        return 1;
    }

    if (fecha_raw_socket(soquete_rx) != 0) {
        return 1;
    }

    char caminho_recebido[256] = {0};
    snprintf(caminho_recebido, sizeof(caminho_recebido),
             "tests/build/integration/network/arquivo_recebido_%ld.txt", (long)getpid());
    FILE *saida = fopen(caminho_recebido, "wb");
    if (saida == NULL) {
        fprintf(stderr, "Falha ao criar TXT recebido\n");
        return 1;
    }

    if (fwrite(payload_recebido, 1, tamanho_recebido, saida) != tamanho_recebido) {
        fclose(saida);
        fprintf(stderr, "Falha ao escrever TXT recebido\n");
        return 1;
    }
    fclose(saida);

    printf("Transferencia de arquivo (%zu bytes) via raw socket validada na interface %s\n",
           lidos, interface);
        printf("Arquivo de origem utilizado: %s\n", TEST_INPUT_FILE_PATH);
    printf("Arquivo recebido salvo em: %s\n", caminho_recebido);
    printf("Conteudo recebido:\n%s\n", payload_recebido);
    return 0;
}