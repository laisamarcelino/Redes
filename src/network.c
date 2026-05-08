#include "../include/network.h"
#include <string.h>
#include <net/if.h>

// Verifica se uma interface é wireless (Wi-Fi)
static int eh_wireless(const char *nome_interface) {
    // Verifica se o nome começa com padrões conhecidos de wireless
    if (strncmp(nome_interface, "wlan", 4) == 0 ||  // wlan0, wlan1, etc
        strncmp(nome_interface, "wlp", 3) == 0 ||   // wlp3s0, etc
        strncmp(nome_interface, "wlx", 3) == 0 ||   // wlx00112233aabb, etc
        strncmp(nome_interface, "ww", 2) == 0)      // ww0, etc
    {
        return 1;
    }
    return 0;
}

// Seleciona a interface de rede (cabo Ethernet ou loopback)
// Retorna: nome da interface alocado (deve ser liberado com free()) ou NULL
// allow_loopback: 1 para selecionar loopback, 0 para selecionar cabo (Ethernet)
char* seleciona_interface_rede(int allow_loopback) {
    struct ifaddrs *ifaddr, *ifa;
    char *interface_selecionada = NULL;

    // Carrega a lista de interfaces do sistema
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return NULL;
    }

    // Percorre todas as interfaces até achar a desejada
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Modo teste: seleciona apenas a interface loopback
        if (allow_loopback) {
            if (strcmp(ifa->ifa_name, "lo") == 0) {
                interface_selecionada = malloc(strlen(ifa->ifa_name) + 1);
                if (interface_selecionada) {
                    strcpy(interface_selecionada, ifa->ifa_name);
                }
                break;
            }
            continue;
        }

        // Modo normal: ignora loopback
        if (strcmp(ifa->ifa_name, "lo") == 0)
            continue;

        // Ignora interfaces wireless
        if (eh_wireless(ifa->ifa_name))
            continue;

        // Seleciona a primeira interface de enlace disponível
        if (ifa->ifa_addr->sa_family == AF_PACKET) {
            interface_selecionada = malloc(strlen(ifa->ifa_name) + 1);
            if (interface_selecionada) {
                strcpy(interface_selecionada, ifa->ifa_name);
            }
            break;
        }
    }

    // Libera a lista de interfaces do sistema
    freeifaddrs(ifaddr);

    if (interface_selecionada == NULL) {
        // Informa falha caso nenhuma interface compatível tenha sido encontrada
        if (allow_loopback) {
            fprintf(stderr, "Erro: nenhuma interface loopback encontrada\n");
        } else {
            fprintf(stderr, "Erro: nenhuma interface de cabo (Ethernet) encontrada\n");
        }
        return NULL;
    }

    // Retorna o nome da interface escolhida
    printf("Interface de rede selecionada: %s\n", interface_selecionada);

    return interface_selecionada;
}

int cria_raw_socket(char* nome_interface_rede) {
    // Cria um socket de camada de enlace para capturar/envio de quadros.
    // Nota: originalmente usávamos `SOCK_RAW` aqui, que fornece quadros
    // incluindo cabeçalhos Ethernet. Contudo, enviar apenas payload com
    // `send()` em muitos kernels resulta em `EINVAL`. Para facilitar o
    // envio/recepção de payload puro (sem cabeçalho) usamos `SOCK_DGRAM`.
    // Linha comentada com a versão original `SOCK_RAW` mantida abaixo.
    // int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    int soquete = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
    if (soquete == -1) {
        fprintf(stderr, "Erro ao criar socket: verifique se você é root!\n");
        return -1;
    }

    // Converte o nome da interface para índice
    int ifindex = if_nametoindex(nome_interface_rede);
    if (ifindex == 0) {
        fprintf(stderr, "Erro: interface de rede inválida: %s\n", nome_interface_rede);
        close(soquete);
        return -1;
    }

    // Prepara o endereço da interface escolhida
    struct sockaddr_ll endereco = {0};
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ifindex;

    // Faz bind do socket na interface selecionada
    if (bind(soquete, (struct sockaddr*) &endereco, sizeof(endereco)) == -1) {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        close(soquete);
        return -1;
    }

    // Ativa o modo promíscuo para receber quadros não reconhecidos pelo SO
    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;

    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1) {
        fprintf(stderr, "Erro ao ativar modo promíscuo\n");
        close(soquete);
        return -1;
    }

    return soquete;
}

// Fica bloqueado esperando uma mensagem chegar no socket
ssize_t espera_mensagem_servidor(int soquete, unsigned char *buffer, size_t tamanho_buffer) {
    struct sockaddr_ll endereco_origem = {0};
    socklen_t tamanho_endereco = sizeof(endereco_origem);

    while (1) {
        ssize_t recebido = recvfrom(soquete,
                                    buffer,
                                    tamanho_buffer,
                                    0,
                                    (struct sockaddr *) &endereco_origem,
                                    &tamanho_endereco);

        if (recebido <= 0) return recebido;

        // Ignora cópias de pacotes originados localmente (evita duplicação)
        // Tipos possíveis: PACKET_HOST, PACKET_BROADCAST, PACKET_MULTICAST,
        // PACKET_OTHERHOST, PACKET_OUTGOING
        if (endereco_origem.sll_pkttype == PACKET_OUTGOING) {
            continue;
        }

        return recebido;
    }
}

// Envia uma mensagem pelo socket já configurado
ssize_t envia_mensagem(int soquete, const unsigned char *buffer, size_t tamanho_buffer) {
    return send(soquete, buffer, tamanho_buffer, 0);
}

// Responde ao cliente com a mensagem "ok"
ssize_t responde_cliente_ok(int soquete) {
    unsigned char resposta[14] = {0};
    resposta[0] = 'o';
    resposta[1] = 'k';
    return envia_mensagem(soquete, resposta, sizeof(resposta));
}

int fecha_raw_socket(int soquete) {
    if (close(soquete) == -1) {
        fprintf(stderr, "Erro ao fechar socket\n");
        return -1;
    }

    return 0;
}