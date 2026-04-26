#include "include/network.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

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

        if (strcmp(entrada->d_name, "lo") != 0) {
            snprintf(destino, tamanho, "%s", entrada->d_name);
            closedir(diretorio);
            return 0;
        }
    }

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

    mac[0] = (unsigned char)b0;
    mac[1] = (unsigned char)b1;
    mac[2] = (unsigned char)b2;
    mac[3] = (unsigned char)b3;
    mac[4] = (unsigned char)b4;
    mac[5] = (unsigned char)b5;
    return 0;
}

int cria_raw_socket(char *nome_interface_rede) {
    // Cria arquivo para o socket sem qualquer protocolo
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (soquete == -1)
    {
        fprintf(stderr, "Erro ao criar socket: Verifique se você é root!\n");
        exit(-1);
    }

    int ifindex = obtem_ifindex_interface(nome_interface_rede);
    if (ifindex < 0) {
        exit(-1);
    }

    struct sockaddr_ll endereco = {0};
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ifindex;
    // Inicializa socket
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1)
    {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        exit(-1);
    }

    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    // Não joga fora o que identifica como lixo: Modo promíscuo
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1)
    {
        fprintf(stderr, "Erro ao fazer setsockopt: "
                        "Verifique se a interface de rede foi especificada corretamente.\n");
        exit(-1);
    }

    return soquete;
}

// Evita que uma chamada de ao socket bloqueie o sistema por tempo indeterminado
int configura_timeout_socket(int soquete, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000
    };

    if (setsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO,
                   (char*) &timeout, sizeof(timeout)) == -1) {
        fprintf(stderr, "Erro ao configurar timeout no socket\n");
        return -1;
    }

    return 0;
}

int fecha_raw_socket(int soquete) {
    if (close(soquete) == -1) { //timeout
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

    ssize_t enviados = sendto(soquete,
                              dados,
                              tamanho_dados,
                              0,
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

    if (endereco_origem != NULL) {
        socklen_t tamanho_origem = sizeof(*endereco_origem);
        ssize_t recebidos = recvfrom(soquete,
                                     buffer,
                                     tamanho_buffer,
                                     0,
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