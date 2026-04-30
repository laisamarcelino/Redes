#include "include/network.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

/* Escolhe uma interface de rede disponível. */
int escolhe_interface_disponivel(char *destino, size_t tamanho) {
    if (destino == NULL || tamanho == 0) {
        fprintf(stderr, "Buffer de interface invalido\n");
        return -1;
    }

    DIR *diretorio = opendir("/sys/class/net");
    struct dirent *entrada;

    if (diretorio == NULL) {
        fprintf(stderr, "Falha ao abrir /sys/class/net\n");
        return -1;
    }

    while ((entrada = readdir(diretorio)) != NULL) {
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        /* Prioriza interfaces não loopback. */
        if (strcmp(entrada->d_name, "lo") != 0) {
            snprintf(destino, tamanho, "%s", entrada->d_name);
            closedir(diretorio);
            return 0;
        }
    }

    /* Se só houver loopback, usa ela. */
    rewinddir(diretorio);
    while ((entrada = readdir(diretorio)) != NULL) {
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) {
            continue;
        }

        snprintf(destino, tamanho, "%s", entrada->d_name);
        closedir(diretorio);
        return 0;
    }

    closedir(diretorio);
    fprintf(stderr, "Nenhuma interface de rede encontrada\n");
    return -1;
}

int obtem_ifindex_interface(const char *nome_interface_rede) {
    if (nome_interface_rede == NULL || nome_interface_rede[0] == '\0') {
        fprintf(stderr, "Interface de rede invalida\n");
        return -1;
    }

    unsigned int ifindex = if_nametoindex(nome_interface_rede);
    if (ifindex == 0) {
        fprintf(stderr, "Interface de rede nao encontrada: %s\n", nome_interface_rede);
        return -1;
    }

    return (int)ifindex;
}

int obtem_mac_interface(const char *nome_interface_rede, unsigned char mac[ETH_ALEN]) {
    if (nome_interface_rede == NULL || nome_interface_rede[0] == '\0' || mac == NULL) {
        fprintf(stderr, "Parametros invalidos para obter MAC\n");
        return -1;
    }

    // Lê o MAC direto do sistema de arquivos do Linux.
    char caminho[256] = {0};
    snprintf(caminho, sizeof(caminho), "/sys/class/net/%s/address", nome_interface_rede);

    FILE *arquivo = fopen(caminho, "r");
    if (arquivo == NULL) {
        fprintf(stderr, "Falha ao abrir arquivo de MAC da interface\n");
        return -1;
    }

    unsigned int b0 = 0;
    unsigned int b1 = 0;
    unsigned int b2 = 0;
    unsigned int b3 = 0;
    unsigned int b4 = 0;
    unsigned int b5 = 0;
    int lidos = fscanf(arquivo, "%x:%x:%x:%x:%x:%x", &b0, &b1, &b2, &b3, &b4, &b5);
    fclose(arquivo);

    if (lidos != 6) {
        fprintf(stderr, "Falha ao interpretar MAC da interface\n");
        return -1;
    }

    /* Converte MAC de texto para bytes. */
    mac[0] = (unsigned char)b0;
    mac[1] = (unsigned char)b1;
    mac[2] = (unsigned char)b2;
    mac[3] = (unsigned char)b3;
    mac[4] = (unsigned char)b4;
    mac[5] = (unsigned char)b5;
    return 0;
}

int cria_raw_socket(char *nome_interface_rede) {
    /* Cria socket raw para ler/enviar quadros Ethernet. */
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (soquete == -1) {
        fprintf(stderr, "Erro ao criar socket: Verifique se você é root!\n");
        return -1;
    }

    int ifindex = obtem_ifindex_interface(nome_interface_rede);
    if (ifindex < 0) {
        close(soquete);
        return -1;
    }

    struct sockaddr_ll endereco = {0};
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ifindex;

    /* Prende o socket na interface escolhida. */
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1) {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        close(soquete);
        return -1;
    }

    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;

    /* Modo promíscuo: captura quadros não endereçados para esta máquina. */
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
        fprintf(stderr, "Erro ao fazer setsockopt: Verifique se a interface está correta.\n");
        close(soquete);
        return -1;
    }

    return soquete;
}

/* Define um tempo máximo de espera no receive. */
int configura_timeout_socket(int soquete, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };

    if (setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO,
                   (char *)&timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Erro ao configurar timeout no socket\n");
        return -1;
    }

    return 0;
}

int fecha_raw_socket(int soquete) {
    if (close(soquete) == -1) {
        fprintf(stderr, "Erro ao fechar socket\n");
        return -1;
    }

    return 0;
}

int configura_endereco_destino_raw(int ifindex, const unsigned char *mac_destino,
                                   struct sockaddr_ll *endereco_destino) {
    if (ifindex <= 0 || endereco_destino == NULL) {
        fprintf(stderr, "Parametros invalidos para endereco raw\n");
        return -1;
    }

    memset(endereco_destino, 0, sizeof(*endereco_destino));
    endereco_destino->sll_family = AF_PACKET;
    endereco_destino->sll_protocol = htons(ETH_P_ALL);
    endereco_destino->sll_ifindex = ifindex;
    endereco_destino->sll_halen = ETH_ALEN;

    /* Só copia MAC se ele foi passado. */
    if (mac_destino != NULL) {
        memcpy(endereco_destino->sll_addr, mac_destino, ETH_ALEN);
    }

    return 0;
}

ssize_t envia_bytes_raw(int soquete, const void *dados, size_t tamanho_dados,
                        const struct sockaddr_ll *endereco_destino) {
    if (dados == NULL || tamanho_dados == 0 || endereco_destino == NULL) {
        fprintf(stderr, "Parametros invalidos para envio raw\n");
        return -1;
    }

    ssize_t enviados = sendto(soquete, dados, tamanho_dados, 0,
                             (const struct sockaddr *)endereco_destino,
                             sizeof(*endereco_destino));
    if (enviados == -1) {
        fprintf(stderr, "Erro ao enviar bytes no raw socket\n");
        return -1;
    }

    return enviados;
}

ssize_t recebe_bytes_raw(int soquete, void *buffer, size_t tamanho_buffer,
                         struct sockaddr_ll *endereco_origem) {
    if (buffer == NULL || tamanho_buffer == 0) {
        fprintf(stderr, "Parametros invalidos para recebimento raw\n");
        return -1;
    }

    /* Se o chamador quiser saber quem enviou, usamos recvfrom. */
    if (endereco_origem != NULL) {
        socklen_t tamanho_origem = sizeof(*endereco_origem);
        ssize_t recebidos = recvfrom(soquete, buffer, tamanho_buffer, 0,
                                    (struct sockaddr *)endereco_origem,
                                    &tamanho_origem);
        if (recebidos == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -1;
            }

            fprintf(stderr, "Erro ao receber bytes no raw socket\n");
            return -1;
        }

        return recebidos;
    }

    /* Recebe apenas os bytes sem informação de origem. */
    ssize_t recebidos = recv(soquete, buffer, tamanho_buffer, 0);
    if (recebidos == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;
        }

        fprintf(stderr, "Erro ao receber bytes no raw socket\n");
        return -1;
    }

    return recebidos;
}

static int obtem_mac_socket(int soquete, unsigned char mac[ETH_ALEN]) {
    struct sockaddr_ll endereco = {0};
    socklen_t tamanho_endereco = sizeof(endereco);
    char nome_interface[IF_NAMESIZE] = {0};

    if (mac == NULL) {
        return -1;
    }

    if (getsockname(soquete, (struct sockaddr *)&endereco, &tamanho_endereco) == -1) {
        fprintf(stderr, "Erro ao obter interface do socket\n");
        return -1;
    }

    if (if_indextoname(endereco.sll_ifindex, nome_interface) == NULL) {
        fprintf(stderr, "Erro ao converter ifindex para nome de interface\n");
        return -1;
    }

    return obtem_mac_interface(nome_interface, mac);
}

/* Envia mensagem com cabeçalho de tamanho.
 *
 * Formato: [Ethernet | cabeçalho(4 bytes) | payload(tamanho variável)]
 * O cabeçalho contém o tamanho do payload em network byte order.
 */
ssize_t envia_mensagem(int soquete, const void *dados, size_t tamanho,
                      const struct sockaddr_ll *endereco_destino) {
    if (dados == NULL || tamanho == 0 || endereco_destino == NULL) {
        fprintf(stderr, "Parametros invalidos para envio de mensagem\n");
        return -1;
    }

    if (tamanho > MENSAGEM_PAYLOAD_MAX) {
        fprintf(stderr, "Mensagem muito grande para um frame Ethernet: %zu > %zu\n",
                tamanho, (size_t)MENSAGEM_PAYLOAD_MAX);
        return -1;
    }

    unsigned char mac_origem[ETH_ALEN] = {0};
    if (obtem_mac_socket(soquete, mac_origem) == -1) {
        return -1;
    }

    size_t tamanho_total = sizeof(struct ethhdr) + sizeof(msg_cabecalho_t) + tamanho;
    unsigned char *buffer = malloc(tamanho_total);
    if (buffer == NULL) {
        fprintf(stderr, "Erro ao alocar memoria para mensagem\n");
        return -1;
    }

    struct ethhdr *eth = (struct ethhdr *)buffer;
    memcpy(eth->h_dest, endereco_destino->sll_addr, ETH_ALEN);
    memcpy(eth->h_source, mac_origem, ETH_ALEN);
    eth->h_proto = htons(ETHER_TYPE_PROTOCOLO_PROPRIO);

    msg_cabecalho_t *cabecalho = (msg_cabecalho_t *)(buffer + sizeof(struct ethhdr));
    cabecalho->tamanho_payload = htonl((unsigned int)tamanho);

    memcpy(buffer + sizeof(struct ethhdr) + sizeof(msg_cabecalho_t), dados, tamanho);

    ssize_t enviados = sendto(soquete, buffer, tamanho_total, 0,
                             (const struct sockaddr *)endereco_destino,
                             sizeof(*endereco_destino));
    free(buffer);

    if (enviados == -1) {
        fprintf(stderr, "Erro ao enviar mensagem no raw socket\n");
        return -1;
    }

    return enviados;
}

/* Recebe mensagem com cabeçalho de tamanho.
 *
 * Formato: [Ethernet | cabeçalho(4 bytes) | payload(tamanho variável)]
 * Extrai e valida o cabeçalho, depois copia apenas o payload para o buffer.
 */
ssize_t recebe_mensagem(int soquete, void *buffer, size_t tamanho_max,
                       struct sockaddr_ll *endereco_origem) {
    if (buffer == NULL || tamanho_max == 0) {
        fprintf(stderr, "Parametros invalidos para recebimento de mensagem\n");
        return -1;
    }

    if (tamanho_max > MENSAGEM_PAYLOAD_MAX) {
        tamanho_max = MENSAGEM_PAYLOAD_MAX;
    }

    size_t tamanho_temp = sizeof(struct ethhdr) + sizeof(msg_cabecalho_t) + tamanho_max;
    unsigned char *buffer_temp = malloc(tamanho_temp);
    if (buffer_temp == NULL) {
        fprintf(stderr, "Erro ao alocar memoria para recebimento\n");
        return -1;
    }

    while (1) {
        struct sockaddr_ll origem_temp = {0};
        struct sockaddr_ll *origem_ptr = endereco_origem != NULL ? endereco_origem : &origem_temp;
        socklen_t tamanho_origem = sizeof(*origem_ptr);
        ssize_t recebidos = recvfrom(soquete, buffer_temp, tamanho_temp, 0,
                                    (struct sockaddr *)origem_ptr,
                                    &tamanho_origem);

        if (recebidos == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                free(buffer_temp);
                return -1;
            }
            fprintf(stderr, "Erro ao receber mensagem no raw socket\n");
            free(buffer_temp);
            return -1;
        }

        if (origem_ptr->sll_pkttype == PACKET_OUTGOING) {
            continue;
        }

        if (recebidos < (ssize_t)(sizeof(struct ethhdr) + sizeof(msg_cabecalho_t))) {
            continue;
        }

        struct ethhdr *eth = (struct ethhdr *)buffer_temp;
        if (ntohs(eth->h_proto) != ETHER_TYPE_PROTOCOLO_PROPRIO) {
            continue;
        }

        msg_cabecalho_t *cabecalho =
            (msg_cabecalho_t *)(buffer_temp + sizeof(struct ethhdr));
        unsigned int tamanho_payload = ntohl(cabecalho->tamanho_payload);
        size_t tamanho_dados_recebidos =
            (size_t)recebidos - sizeof(struct ethhdr) - sizeof(msg_cabecalho_t);

        if (tamanho_dados_recebidos < tamanho_payload) {
            fprintf(stderr, "Tamanho de payload inconsistente: esperado %u, recebido %zu\n",
                    tamanho_payload, tamanho_dados_recebidos);
            continue;
        }

        if (tamanho_payload > tamanho_max) {
            fprintf(stderr, "Payload muito grande: %u > %zu\n", tamanho_payload, tamanho_max);
            continue;
        }

        memcpy(buffer,
               buffer_temp + sizeof(struct ethhdr) + sizeof(msg_cabecalho_t),
               tamanho_payload);
        free(buffer_temp);
        return (ssize_t)tamanho_payload;
    }
}
