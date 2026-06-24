#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "network.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

/*
 * Define um EtherType próprio para o projeto
 * Com isso o servidor deixa de processar ARP, IPv6 e outros pacotes da rede
 */
#define ETH_P_PACMAN 0x88B5

/*
 * Guarda dados da interface configurada
 * envia_mensagem() usa essas informações para montar o quadro Ethernet
 */
static int g_ifindex = 0;
static unsigned char g_mac_origem[ETH_ALEN];
static char g_nome_interface[IFNAMSIZ];

/* Retorna verdadeiro se o nome da interface tem prefixo típico de Wi-Fi */
static int eh_wireless(const char *nome_interface)
{
    if (strncmp(nome_interface, "wlan", 4) == 0 ||
        strncmp(nome_interface, "wlp", 3) == 0 ||
        strncmp(nome_interface, "wlx", 3) == 0 ||
        strncmp(nome_interface, "ww", 2) == 0)
    {
        return 1;
    }
    return 0;
}

/* Retorna verdadeiro se a interface é virtual (Docker, bridge, VPN, etc.) */
static int eh_virtual(const char *nome_interface)
{
    if (strcmp(nome_interface, "docker0") == 0 ||
        strncmp(nome_interface, "br-", 3) == 0 ||
        strncmp(nome_interface, "veth", 4) == 0 ||
        strncmp(nome_interface, "virbr", 5) == 0 ||
        strncmp(nome_interface, "tun", 3) == 0 ||
        strncmp(nome_interface, "tap", 3) == 0)
    {
        return 1;
    }
    return 0;
}

/* Retorna verdadeiro se o nome da interface tem prefixo típico de cabo Ethernet */
static int eh_ethernet(const char *nome_interface)
{
    if (strncmp(nome_interface, "eth", 3) == 0 ||
        strncmp(nome_interface, "en", 2) == 0)
    {
        return 1;
    }
    return 0;
}

/* Lê o endereço MAC da interface de rede e o copia para o buffer mac */
static int obtem_mac_interface(int soquete, const char *nome_interface, unsigned char mac[ETH_ALEN])
{
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    strncpy(ifr.ifr_name, nome_interface, IFNAMSIZ - 1);
    if (ioctl(soquete, SIOCGIFHWADDR, &ifr) == -1)
    {
        perror("ioctl(SIOCGIFHWADDR)");
        return -1;
    }

    memcpy(mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    return 0;
}

/* Retorna o tempo atual em milissegundos desde a época Unix */
static long long timestamp_ms(void)
{
    struct timeval tp;

    gettimeofday(&tp, NULL);

    return ((long long)tp.tv_sec * 1000LL) + ((long long)tp.tv_usec / 1000LL);
}

/* Remove o timeout de recebimento do socket, tornando-o bloqueante novamente */
static int limpa_timeout_recebimento(int soquete)
{
    struct timeval timeout;

    memset(&timeout, 0, sizeof(timeout));

    if (setsockopt(
            soquete,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)) == -1)
    {
        perror("setsockopt(SO_RCVTIMEO reset)");
        return -1;
    }

    return 0;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* Percorre as interfaces do sistema e retorna o nome da primeira elegível para o protocolo */
char *seleciona_interface_rede(int allow_loopback)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    char *interface_selecionada = NULL;
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return NULL;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }

        // Loopback fica restrito ao modo de teste para não mascarar o uso real por cabo.
        if (allow_loopback)
        {
            if (strcmp(ifa->ifa_name, "lo") == 0 && ifa->ifa_addr->sa_family == AF_PACKET)
            {
                interface_selecionada = malloc(strlen(ifa->ifa_name) + 1);
                if (interface_selecionada != NULL)
                {
                    strcpy(interface_selecionada, ifa->ifa_name);
                }
                break;
            }
            continue;
        }

        if (ifa->ifa_addr->sa_family != AF_PACKET)
        {
            continue;
        }

        // O trabalho usa enlace Ethernet físico; interfaces virtuais e Wi-Fi são ignoradas.
        if (strcmp(ifa->ifa_name, "lo") == 0 ||
            eh_wireless(ifa->ifa_name) ||
            eh_virtual(ifa->ifa_name) ||
            !eh_ethernet(ifa->ifa_name))
        {
            continue;
        }

        if ((ifa->ifa_flags & IFF_UP) == 0)
        {
            continue;
        }

        interface_selecionada = malloc(strlen(ifa->ifa_name) + 1);
        if (interface_selecionada != NULL)
        {
            strcpy(interface_selecionada, ifa->ifa_name);
        }
        break;
    }
    freeifaddrs(ifaddr);

    if (interface_selecionada == NULL)
    {
        if (allow_loopback)
        {
            fprintf(stderr, "Erro: nenhuma interface loopback encontrada\n");
        }
        else
        {
            fprintf(stderr, "Erro: nenhuma interface de cabo Ethernet encontrada\n");
        }
        return NULL;
    }
    printf("Interface de rede selecionada: %s\n", interface_selecionada);
    return interface_selecionada;
}

/* Cria e configura o socket raw ligado à interface, pronto para enviar e receber quadros PacMan */
int cria_raw_socket(char *nome_interface_rede)
{
    if (nome_interface_rede == NULL)
    {
        fprintf(stderr, "Erro: interface de rede nula\n");
        return -1;
    }

    // SOCK_RAW permite montar o quadro Ethernet completo exigido pelo protocolo do projeto.
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_PACMAN));
    if (soquete == -1)
    {
        fprintf(stderr, "Erro ao criar socket: verifique se você é root!\n");
        perror("socket");
        return -1;
    }

    int ifindex = (int)if_nametoindex(nome_interface_rede);
    if (ifindex == 0)
    {
        fprintf(stderr, "Erro: interface de rede inválida: %s\n", nome_interface_rede);
        close(soquete);
        return -1;
    }

    if (obtem_mac_interface(soquete, nome_interface_rede, g_mac_origem) == -1)
    {
        close(soquete);
        return -1;
    }
    g_ifindex = ifindex;
    strncpy(g_nome_interface, nome_interface_rede, IFNAMSIZ - 1);
    g_nome_interface[IFNAMSIZ - 1] = '\0';

    struct sockaddr_ll endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_PACMAN);
    endereco.sll_ifindex = ifindex;

    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1)
    {
        perror("bind");
        close(soquete);
        return -1;
    }

    // Modo promíscuo facilita capturar os quadros do protocolo durante testes em laboratório.
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;

    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1)
    {
        perror("setsockopt(PACKET_ADD_MEMBERSHIP)");
        close(soquete);
        return -1;
    }

    printf("MAC de origem: %02x:%02x:%02x:%02x:%02x:%02x\n",
           g_mac_origem[0], g_mac_origem[1], g_mac_origem[2],
           g_mac_origem[3], g_mac_origem[4], g_mac_origem[5]);

    return soquete;
}

/* Bloqueia até receber um quadro PacMan válido e copia o payload para o buffer */
ssize_t espera_mensagem_servidor(int *p_soquete, unsigned char *buffer, size_t tamanho_buffer)
{
    unsigned char quadro[TAM_BUFFER_RAW];
    struct sockaddr_ll endereco_origem;
    socklen_t tamanho_endereco;

    while (1)
    {
        memset(&endereco_origem, 0, sizeof(endereco_origem));
        tamanho_endereco = sizeof(endereco_origem);

        ssize_t recebido = recvfrom(*p_soquete,
                                    quadro,
                                    sizeof(quadro),
                                    0,
                                    (struct sockaddr *)&endereco_origem,
                                    &tamanho_endereco);

        if (recebido < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == ENETDOWN  || errno == ENETUNREACH ||
                errno == ENXIO     || errno == ENOBUFS)
            {
                aguarda_link_e_recria_socket(p_soquete);
                continue;
            }
            return -1;
        }

        if (recebido == 0)
            return 0;

        // Raw sockets também enxergam cópias locais; elas não devem virar mensagens recebidas.
        if (endereco_origem.sll_pkttype == PACKET_OUTGOING)
        {
            continue;
        }

        if ((size_t)recebido < sizeof(struct ether_header))
        {
            continue;
        }

        struct ether_header *eth = (struct ether_header *)quadro;

        if (ntohs(eth->ether_type) != ETH_P_PACMAN)
        {
            continue;
        }

        unsigned char *payload = quadro + sizeof(struct ether_header);
        size_t tamanho_payload = (size_t)recebido - sizeof(struct ether_header);

        if (tamanho_payload < TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_CRC_PROTOCOLO)
        {
            continue;
        }

        if (payload[0] != MARCADOR_INICIO)
        {
            continue;
        }

        uint8_t tamanho_dados = (payload[1] >> 3) & 0x1F;
        size_t tamanho_pacote = TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados + TAMANHO_CRC_PROTOCOLO;

        if (tamanho_pacote > tamanho_payload)
        {
            continue;
        }

        if (tamanho_pacote > tamanho_buffer)
        {
            errno = EMSGSIZE;
            return -1;
        }

        memcpy(buffer, payload, tamanho_pacote);
        return (ssize_t)tamanho_pacote;
    }
}

/* Monta um quadro Ethernet com o payload informado e o transmite pela interface configurada */
ssize_t envia_mensagem(int soquete, const unsigned char *buffer, size_t tamanho_buffer)
{
    if (buffer == NULL || tamanho_buffer == 0)
    {
        errno = EINVAL;
        return -1;
    }

    /*
     * g_ifindex e g_mac_origem sao preenchidos em cria_raw_socket()
     * Sem esse indice nao sabemos por qual placa de rede o quadro deve sair
     */
    if (g_ifindex == 0)
    {
        fprintf(stderr, "Erro: socket ainda não possui interface configurada\n");
        errno = EINVAL;
        return -1;
    }

    /*
     * O limite considera quadro Ethernet completo, nao apenas o pacote PacMan.
     */
    if (sizeof(struct ether_header) + tamanho_buffer > TAM_BUFFER_RAW)
    {
        fprintf(stderr, "Erro: mensagem grande demais para o buffer raw\n");
        errno = EMSGSIZE;
        return -1;
    }

    unsigned char quadro[TAM_BUFFER_RAW];
    memset(quadro, 0, sizeof(quadro));

    struct ether_header *eth = (struct ether_header *)quadro;

    // Broadcast evita depender de descoberta de MAC entre as duas máquinas do teste.
    memset(eth->ether_dhost, 0xff, ETH_ALEN);

    memcpy(eth->ether_shost, g_mac_origem, ETH_ALEN);

    /*
     * EtherType próprio separa os quadros PacMan do restante do tráfego local.
     */
    eth->ether_type = htons(ETH_P_PACMAN);

    memcpy(quadro + sizeof(struct ether_header), buffer, tamanho_buffer);

    struct sockaddr_ll endereco_destino;
    memset(&endereco_destino, 0, sizeof(endereco_destino));

    /*
     * sockaddr_ll fixa a saída na interface escolhida para não depender da rota do sistema.
     */
    endereco_destino.sll_family = AF_PACKET;
    endereco_destino.sll_ifindex = g_ifindex;
    endereco_destino.sll_halen = ETH_ALEN;
    memset(endereco_destino.sll_addr, 0xff, ETH_ALEN);

    size_t tamanho_quadro = sizeof(struct ether_header) + tamanho_buffer;

    ssize_t enviado = sendto(soquete,
                             quadro,
                             tamanho_quadro,
                             0,
                             (struct sockaddr *)&endereco_destino,
                             sizeof(endereco_destino));

    if (enviado < 0)
    {
        return enviado;
    }

    if ((size_t)enviado < sizeof(struct ether_header))
    {
        errno = EIO;
        return -1;
    }

    return enviado - (ssize_t)sizeof(struct ether_header);
}

/* Fecha o descritor do socket e libera o recurso no sistema operacional */
int fecha_raw_socket(int soquete)
{
    if (close(soquete) == -1)
    {
        fprintf(stderr, "Erro ao fechar socket\n");
        return -1;
    }

    return 0;
}

/* Detecta a queda do cabo, aguarda a reconexão e recria o socket na mesma interface */
int aguarda_link_e_recria_socket(int *p_soquete)
{
    if (p_soquete == NULL)
        return -1;

    fprintf(stderr, "\n[REDE] Cabo desconectado. Aguardando reconexao...\n");

    if (*p_soquete >= 0)
    {
        close(*p_soquete);
        *p_soquete = -1;
    }

    while (1)
    {
        int tmp = socket(AF_INET, SOCK_DGRAM, 0);
        if (tmp >= 0)
        {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, g_nome_interface, IFNAMSIZ - 1);

            int link_ativo = 0;
            if (ioctl(tmp, SIOCGIFFLAGS, &ifr) == 0 &&
                (ifr.ifr_flags & IFF_RUNNING))
                link_ativo = 1;

            close(tmp);
            if (link_ativo)
                break;
        }

        struct timespec ts = { 0, 500000000L };
        nanosleep(&ts, NULL);
    }

    fprintf(stderr, "[REDE] Link restabelecido. Recriando socket...\n");

    int novo = cria_raw_socket(g_nome_interface);
    if (novo < 0)
    {
        fprintf(stderr, "[ERRO] Falha ao recriar socket apos reconexao\n");
        return -1;
    }

    *p_soquete = novo;
    return 0;
}

/* Aguarda um quadro PacMan válido por no máximo timeout_ms milissegundos */
ssize_t espera_mensagem_timeout(int *p_soquete, unsigned char *buffer,
                                size_t tamanho_buffer, int timeout_ms)
{
    unsigned char quadro[TAM_BUFFER_RAW];
    struct sockaddr_ll endereco_origem;
    socklen_t tamanho_endereco;

    long long inicio;
    long long agora;
    long long decorrido;
    long long restante;

    if (buffer == NULL || tamanho_buffer == 0 || timeout_ms <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    inicio = timestamp_ms();

    while (1)
    {
        agora = timestamp_ms();
        decorrido = agora - inicio;

        if (decorrido >= timeout_ms)
        {
            limpa_timeout_recebimento(*p_soquete);
            return REDE_TIMEOUT;
        }

        restante = timeout_ms - decorrido;
        struct timeval timeout;
        timeout.tv_sec = (time_t)(restante / 1000);
        timeout.tv_usec = (suseconds_t)((restante % 1000) * 1000);

        if (setsockopt(
                *p_soquete,
                SOL_SOCKET,
                SO_RCVTIMEO,
                &timeout,
                sizeof(timeout)) == -1)
        {
            perror("setsockopt(SO_RCVTIMEO)");
            limpa_timeout_recebimento(*p_soquete);
            return -1;
        }

        memset(&endereco_origem, 0, sizeof(endereco_origem));
        tamanho_endereco = sizeof(endereco_origem);

        ssize_t recebido = recvfrom(
            *p_soquete,
            quadro,
            sizeof(quadro),
            0,
            (struct sockaddr *)&endereco_origem,
            &tamanho_endereco);

        if (recebido < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == ENETDOWN  || errno == ENETUNREACH ||
                errno == ENXIO     || errno == ENOBUFS)
            {
                aguarda_link_e_recria_socket(p_soquete);
                // O prazo da aplicação recomeça após a reconexão física do cabo.
                inicio = timestamp_ms();
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                limpa_timeout_recebimento(*p_soquete);
                return REDE_TIMEOUT;
            }

            limpa_timeout_recebimento(*p_soquete);
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        // Raw sockets também enxergam cópias locais; elas não devem virar mensagens recebidas.
        if (endereco_origem.sll_pkttype == PACKET_OUTGOING)
        {
            continue;
        }

        if ((size_t)recebido < sizeof(struct ether_header))
        {
            continue;
        }

        struct ether_header *eth = (struct ether_header *)quadro;

        if (ntohs(eth->ether_type) != ETH_P_PACMAN)
        {
            continue;
        }

        unsigned char *payload = quadro + sizeof(struct ether_header);
        size_t tamanho_payload = (size_t)recebido - sizeof(struct ether_header);

        if (tamanho_payload < TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_CRC_PROTOCOLO)
        {
            continue;
        }

        if (payload[0] != MARCADOR_INICIO)
        {
            continue;
        }

        uint8_t tamanho_dados = (payload[1] >> 3) & 0x1F;
        size_t tamanho_pacote = TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados + TAMANHO_CRC_PROTOCOLO;

        if (tamanho_pacote > tamanho_payload)
        {
            continue;
        }

        if (tamanho_pacote > tamanho_buffer)
        {
            errno = EMSGSIZE;
            limpa_timeout_recebimento(*p_soquete);
            return -1;
        }

        // O padding Ethernet é descartado para a validação trabalhar só com o pacote PacMan.
        memcpy(buffer, payload, tamanho_pacote);

        limpa_timeout_recebimento(*p_soquete);
        return (ssize_t)tamanho_pacote;
    }
}
