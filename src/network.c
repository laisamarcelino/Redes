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

// Verifica se uma interface é wireless pelo padrão mais comum de nomes Linux
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

// Verifica interfaces virtuais comuns que nao representam o cabo Ethernet
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

// Verifica os padroes mais comuns de nomes para Ethernet fisica no Linux
static int eh_ethernet(const char *nome_interface)
{
    if (strncmp(nome_interface, "eth", 3) == 0 ||
        strncmp(nome_interface, "en", 2) == 0)
    {
        return 1;
    }
    return 0;
}

// Obtém o MAC da interface para preencher o endereço de origem do quadro
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

static long long timestamp_ms(void)
{
    struct timeval tp;

    gettimeofday(&tp, NULL);

    return ((long long)tp.tv_sec * 1000LL) + ((long long)tp.tv_usec / 1000LL);
}

// Remove o timeout de recebimento e devolve o socket ao modo bloqueante
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

// Seleciona a interface de rede Ethernet ou loopback
char *seleciona_interface_rede(int allow_loopback)
{
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa = NULL;
    char *interface_selecionada = NULL;

    // Carrega a lista de interfaces do sistema
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        return NULL;
    }

    // Percorre todas as interfaces até achar a desejada
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }

        // Em teste local, seleciona apenas a interface loopback
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

        // Em uso real, ignora loopback, wireless e interfaces virtuais
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

        // Seleciona a primeira interface de enlace disponível
        interface_selecionada = malloc(strlen(ifa->ifa_name) + 1);
        if (interface_selecionada != NULL)
        {
            strcpy(interface_selecionada, ifa->ifa_name);
        }
        break;
    }

    // Libera a lista de interfaces do sistema
    freeifaddrs(ifaddr);

    if (interface_selecionada == NULL)
    {
        // Informa falha caso nenhuma interface compatível tenha sido encontrada
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

    // Retorna o nome da interface escolhida
    printf("Interface de rede selecionada: %s\n", interface_selecionada);
    return interface_selecionada;
}

int cria_raw_socket(char *nome_interface_rede)
{
    if (nome_interface_rede == NULL)
    {
        fprintf(stderr, "Erro: interface de rede nula\n");
        return -1;
    }

    /*
     * APAGAR
     * Usa SOCK_RAW e monta o cabeçalho Ethernet completo
     * O envio deixa de depender de payload solto via send()
     */
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_PACMAN));
    if (soquete == -1)
    {
        fprintf(stderr, "Erro ao criar socket: verifique se você é root!\n");
        perror("socket");
        return -1;
    }

    // Converte o nome da interface para índice
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

    // Prepara o endereço da interface escolhida
    struct sockaddr_ll endereco;
    memset(&endereco, 0, sizeof(endereco));
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_PACMAN);
    endereco.sll_ifindex = ifindex;

    // Faz bind do socket na interface selecionada
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1)
    {
        perror("bind");
        close(soquete);
        return -1;
    }

    // Mantém modo promíscuo para facilitar testes
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

// Espera um quadro do protocolo PacMan e entrega apenas o payload
ssize_t espera_mensagem_servidor(int soquete, unsigned char *buffer, size_t tamanho_buffer)
{
    unsigned char quadro[TAM_BUFFER_RAW];
    struct sockaddr_ll endereco_origem;
    socklen_t tamanho_endereco;

    while (1)
    {
        memset(&endereco_origem, 0, sizeof(endereco_origem));
        tamanho_endereco = sizeof(endereco_origem);

        ssize_t recebido = recvfrom(soquete,
                                    quadro,
                                    sizeof(quadro),
                                    0,
                                    (struct sockaddr *)&endereco_origem,
                                    &tamanho_endereco);

        if (recebido < 0)
        {
            if (errno == EINTR   || errno == ENETDOWN  ||
                errno == ENETUNREACH || errno == ENXIO ||
                errno == ENOBUFS)
                continue;
            return -1;
        }

        if (recebido == 0)
            return 0;

        // Ignora cópias locais do próprio pacote enviado
        if (endereco_origem.sll_pkttype == PACKET_OUTGOING)
        {
            continue;
        }

        if ((size_t)recebido < sizeof(struct ether_header))
        {
            continue;
        }

        struct ether_header *eth = (struct ether_header *)quadro;

        // Descarta tudo que não tenha o EtherType do projeto
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

// Envia uma mensagem montando um quadro Ethernet completo
ssize_t envia_mensagem(int soquete, const unsigned char *buffer, size_t tamanho_buffer)
{
    // Nao ha o que enviar se o ponteiro for nulo ou o payload estiver vazio.
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
     * O quadro Ethernet final contem:
     *   cabecalho Ethernet + payload recebido em buffer
     * TAM_BUFFER_RAW limita o tamanho total que esta funcao consegue montar
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

    /* 
     * Usa broadcast como MAC destino no primeiro teste por cabo
     * Assim não é necessário descobrir o MAC da outra máquina nesta etapa
     * ff:ff:ff:ff:ff:ff faz a placa enviar o quadro para todos no enlace local
     */
    memset(eth->ether_dhost, 0xff, ETH_ALEN);

    // MAC de origem: endereco fisico da interface escolhida no socket
    memcpy(eth->ether_shost, g_mac_origem, ETH_ALEN);

    /*
     * EtherType identifica o "protocolo" carregado no quadro
     * htons() converte para ordem de bytes de rede, como o Ethernet espera
     */
    eth->ether_type = htons(ETH_P_PACMAN);

    // Copia a mensagem logo apos o cabecalho Ethernet
    memcpy(quadro + sizeof(struct ether_header), buffer, tamanho_buffer);

    struct sockaddr_ll endereco_destino;
    memset(&endereco_destino, 0, sizeof(endereco_destino));

    /*
     * sockaddr_ll informa ao kernel que este envio e de camada 2 (AF_PACKET),
     * por qual interface fisica deve sair e qual endereco MAC sera usado
     */
    endereco_destino.sll_family = AF_PACKET;
    endereco_destino.sll_ifindex = g_ifindex;
    endereco_destino.sll_halen = ETH_ALEN;
    memset(endereco_destino.sll_addr, 0xff, ETH_ALEN);

    size_t tamanho_quadro = sizeof(struct ether_header) + tamanho_buffer;

    // sendto() envia o quadro Ethernet ja montado
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

    // Se nem o cabecalho inteiro foi aceito como enviado, o envio e invalido
    if ((size_t)enviado < sizeof(struct ether_header))
    {
        errno = EIO;
        return -1;
    }

    // A interface publica da funcao retorna apenas quantos bytes de payload sairam
    return enviado - (ssize_t)sizeof(struct ether_header);
}

int fecha_raw_socket(int soquete)
{
    if (close(soquete) == -1)
    {
        fprintf(stderr, "Erro ao fechar socket\n");
        return -1;
    }

    return 0;
}

ssize_t espera_mensagem_timeout(int soquete, unsigned char *buffer,
                                size_t tamanho_buffer, int timeout_ms)
{
    // Buffer temporario para o quadro Ethernet completo
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

    // Marca o inicio da espera
    inicio = timestamp_ms();

    while (1)
    {
        // Atualiza o tempo ja gasto nesta espera
        agora = timestamp_ms();
        decorrido = agora - inicio;

        // Encerra quando o limite total foi atingido
        if (decorrido >= timeout_ms)
        {
            limpa_timeout_recebimento(soquete);
            return REDE_TIMEOUT;
        }

        // Calcula quanto tempo ainda pode bloquear
        restante = timeout_ms - decorrido;

        /* APAGAR
         * Define o timeout do recvfrom para o tempo restante.
         * Mesmo assim mantemos nosso próprio relógio, como sugerido
         * na fonte do Todt
         */
        // Define o timeout do recvfrom para o tempo restante
        struct timeval timeout;
        timeout.tv_sec = (time_t)(restante / 1000);
        timeout.tv_usec = (suseconds_t)((restante % 1000) * 1000);

        // Aplica o timeout no socket antes da leitura
        if (setsockopt(
                soquete,
                SOL_SOCKET,
                SO_RCVTIMEO,
                &timeout,
                sizeof(timeout)) == -1)
        {
            perror("setsockopt(SO_RCVTIMEO)");
            limpa_timeout_recebimento(soquete);
            return -1;
        }

        // Limpa o endereco antes de receber o proximo quadro
        memset(&endereco_origem, 0, sizeof(endereco_origem));
        tamanho_endereco = sizeof(endereco_origem);

        // Recebe um quadro Ethernet bruto
        ssize_t recebido = recvfrom(
            soquete,
            quadro,
            sizeof(quadro),
            0,
            (struct sockaddr *)&endereco_origem,
            &tamanho_endereco);

        // Trata timeout, interrupcao e erro de rede
        if (recebido < 0)
        {
            if (errno == EINTR   || errno == ENETDOWN  ||
                errno == ENETUNREACH || errno == ENXIO ||
                errno == ENOBUFS)
            {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                limpa_timeout_recebimento(soquete);
                return REDE_TIMEOUT;
            }

            limpa_timeout_recebimento(soquete);
            return -1;
        }

        // Leituras vazias sao ignoradas
        if (recebido == 0)
        {
            continue;
        }

        // Ignora cópias locais do próprio pacote enviado
        if (endereco_origem.sll_pkttype == PACKET_OUTGOING)
        {
            continue;
        }

        // Um quadro menor que o cabecalho Ethernet eh invalido
        if ((size_t)recebido < sizeof(struct ether_header))
        {
            continue;
        }

        // Interpreta os primeiros bytes como cabecalho Ethernet
        struct ether_header *eth = (struct ether_header *)quadro;

        /*
         * Descarta pacotes que não são do protocolo PacMan
         */
        if (ntohs(eth->ether_type) != ETH_P_PACMAN)
        {
            continue;
        }

        // Payload comeca logo depois do cabecalho Ethernet
        unsigned char *payload = quadro + sizeof(struct ether_header);
        size_t tamanho_payload = (size_t)recebido - sizeof(struct ether_header);

        // Pacote PacMan minimo precisa ter cabecalho e CRC
        if (tamanho_payload < TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_CRC_PROTOCOLO)
        {
            continue;
        }

        // Primeiro byte deve ser o marcador do protocolo
        if (payload[0] != MARCADOR_INICIO)
        {
            continue;
        }

        // Extrai o tamanho de dados codificado no cabecalho
        uint8_t tamanho_dados = (payload[1] >> 3) & 0x1F;
        size_t tamanho_pacote = TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados + TAMANHO_CRC_PROTOCOLO;

        // Quadro veio incompleto
        if (tamanho_pacote > tamanho_payload)
        {
            continue;
        }

        // O buffer de destino precisa caber o pacote real
        if (tamanho_pacote > tamanho_buffer)
        {
            errno = EMSGSIZE;
            limpa_timeout_recebimento(soquete);
            return -1;
        }

        // Copia apenas o pacote PacMan, sem padding Ethernet
        memcpy(buffer, payload, tamanho_pacote);

        // Retorna o tamanho real do pacote PacMan
        limpa_timeout_recebimento(soquete);
        return (ssize_t)tamanho_pacote;
    }
}
