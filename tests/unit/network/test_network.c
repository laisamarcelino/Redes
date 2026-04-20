#include "include/network.h"

#include <errno.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int fake_socket_return = 42;
static int fake_bind_return = 0;
static int fake_setsockopt_return = 0;
static int fake_close_return = 0;
static unsigned int fake_ifindex_return = 7;

static int socket_called = 0;
static int bind_called = 0;
static int setsockopt_called = 0;
static int close_called = 0;

static int last_socket_domain = 0;
static int last_socket_type = 0;
static int last_socket_protocol = 0;

static int last_bind_socket = 0;
static socklen_t last_bind_addrlen = 0;
static struct sockaddr_ll last_bind_addr;

static int last_setsockopt_socket = 0;
static int last_setsockopt_level = 0;
static int last_setsockopt_optname = 0;
static socklen_t last_setsockopt_optlen = 0;
static struct packet_mreq last_packet_mreq;
static struct timeval last_timeval;

static int last_close_socket = 0;
static char last_ifname[IF_NAMESIZE];

static int exit_called = 0;
static int exit_status_code = 0;
static jmp_buf exit_jmp;

static void reset_fakes(void) {
    fake_socket_return = 42;
    fake_bind_return = 0;
    fake_setsockopt_return = 0;
    fake_close_return = 0;
    fake_ifindex_return = 7;

    socket_called = 0;
    bind_called = 0;
    setsockopt_called = 0;
    close_called = 0;

    last_socket_domain = 0;
    last_socket_type = 0;
    last_socket_protocol = 0;

    last_bind_socket = 0;
    last_bind_addrlen = 0;
    memset(&last_bind_addr, 0, sizeof(last_bind_addr));

    last_setsockopt_socket = 0;
    last_setsockopt_level = 0;
    last_setsockopt_optname = 0;
    last_setsockopt_optlen = 0;
    memset(&last_packet_mreq, 0, sizeof(last_packet_mreq));
    memset(&last_timeval, 0, sizeof(last_timeval));

    last_close_socket = 0;
    memset(last_ifname, 0, sizeof(last_ifname));

    exit_called = 0;
    exit_status_code = 0;
}

static int test_socket(int domain, int type, int protocol) {
    socket_called++;
    last_socket_domain = domain;
    last_socket_type = type;
    last_socket_protocol = protocol;
    return fake_socket_return;
}

static unsigned int test_if_nametoindex(const char *ifname) {
    snprintf(last_ifname, sizeof(last_ifname), "%s", ifname);
    return fake_ifindex_return;
}

static int test_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    bind_called++;
    last_bind_socket = sockfd;
    last_bind_addrlen = addrlen;
    memcpy(&last_bind_addr, addr, sizeof(last_bind_addr));
    return fake_bind_return;
}

static int test_setsockopt(
    int sockfd,
    int level,
    int optname,
    const void *optval,
    socklen_t optlen
) {
    setsockopt_called++;
    last_setsockopt_socket = sockfd;
    last_setsockopt_level = level;
    last_setsockopt_optname = optname;
    last_setsockopt_optlen = optlen;

    if (level == SOL_PACKET && optname == PACKET_ADD_MEMBERSHIP) {
        memcpy(&last_packet_mreq, optval, sizeof(last_packet_mreq));
    } else if (level == SOL_SOCKET && optname == SO_RCVTIMEO) {
        memcpy(&last_timeval, optval, sizeof(last_timeval));
    }

    return fake_setsockopt_return;
}

static int test_close(int fd) {
    close_called++;
    last_close_socket = fd;
    return fake_close_return;
}

static void test_exit(int status) {
    exit_called = 1;
    exit_status_code = status;
    longjmp(exit_jmp, 1);
}

#define socket test_socket
#define if_nametoindex test_if_nametoindex
#define bind test_bind
#define setsockopt test_setsockopt
#define close test_close
#define exit test_exit
#include "../../../src/network.c"
#undef socket
#undef if_nametoindex
#undef bind
#undef setsockopt
#undef close
#undef exit

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int test_cria_raw_socket_success(void) {
    reset_fakes();

    int soquete = cria_raw_socket("eth0");

    if (!assert_true(soquete == fake_socket_return, "cria_raw_socket deve retornar o descritor criado")) return 0;
    if (!assert_true(socket_called == 1, "socket deve ser chamado uma vez")) return 0;
    if (!assert_true(last_socket_domain == AF_PACKET, "socket deve usar AF_PACKET")) return 0;
    if (!assert_true(last_socket_type == SOCK_RAW, "socket deve usar SOCK_RAW")) return 0;
    if (!assert_true(last_socket_protocol == htons(ETH_P_ALL), "socket deve usar ETH_P_ALL")) return 0;
    if (!assert_true(strcmp(last_ifname, "eth0") == 0, "if_nametoindex deve receber a interface informada")) return 0;
    if (!assert_true(bind_called == 1, "bind deve ser chamado uma vez")) return 0;
    if (!assert_true(last_bind_socket == fake_socket_return, "bind deve usar o descritor criado")) return 0;
    if (!assert_true(last_bind_addr.sll_family == AF_PACKET, "bind deve usar AF_PACKET")) return 0;
    if (!assert_true(last_bind_addr.sll_protocol == htons(ETH_P_ALL), "bind deve usar ETH_P_ALL")) return 0;
    if (!assert_true(last_bind_addr.sll_ifindex == (int)fake_ifindex_return, "bind deve usar o ifindex retornado")) return 0;
    if (!assert_true(setsockopt_called == 1, "setsockopt deve ser chamado uma vez na criação")) return 0;
    if (!assert_true(last_setsockopt_level == SOL_PACKET, "setsockopt deve configurar SOL_PACKET")) return 0;
    if (!assert_true(last_setsockopt_optname == PACKET_ADD_MEMBERSHIP, "setsockopt deve habilitar PACKET_ADD_MEMBERSHIP")) return 0;
    if (!assert_true(last_packet_mreq.mr_ifindex == (int)fake_ifindex_return, "packet_mreq deve usar o ifindex retornado")) return 0;
    if (!assert_true(last_packet_mreq.mr_type == PACKET_MR_PROMISC, "packet_mreq deve ativar modo promiscuo")) return 0;

    return 1;
}

static int test_cria_raw_socket_socket_failure(void) {
    reset_fakes();
    fake_socket_return = -1;

    if (setjmp(exit_jmp) == 0) {
        cria_raw_socket("eth0");
        return assert_true(0, "cria_raw_socket deveria encerrar em falha de socket");
    }

    if (!assert_true(exit_called == 1, "exit deve ser chamado em falha de socket")) return 0;
    if (!assert_true(exit_status_code == -1, "exit deve usar codigo -1 em falha de socket")) return 0;
    if (!assert_true(bind_called == 0, "bind nao deve ser chamado apos falha de socket")) return 0;
    if (!assert_true(setsockopt_called == 0, "setsockopt nao deve ser chamado apos falha de socket")) return 0;

    return 1;
}

static int test_cria_raw_socket_bind_failure(void) {
    reset_fakes();
    fake_bind_return = -1;

    if (setjmp(exit_jmp) == 0) {
        cria_raw_socket("eth0");
        return assert_true(0, "cria_raw_socket deveria encerrar em falha de bind");
    }

    if (!assert_true(exit_called == 1, "exit deve ser chamado em falha de bind")) return 0;
    if (!assert_true(exit_status_code == -1, "exit deve usar codigo -1 em falha de bind")) return 0;
    if (!assert_true(bind_called == 1, "bind deve ter sido chamado")) return 0;
    if (!assert_true(setsockopt_called == 0, "setsockopt nao deve ser chamado apos falha de bind")) return 0;

    return 1;
}

static int test_cria_raw_socket_setsockopt_failure(void) {
    reset_fakes();
    fake_setsockopt_return = -1;

    if (setjmp(exit_jmp) == 0) {
        cria_raw_socket("eth0");
        return assert_true(0, "cria_raw_socket deveria encerrar em falha de setsockopt");
    }

    if (!assert_true(exit_called == 1, "exit deve ser chamado em falha de setsockopt")) return 0;
    if (!assert_true(exit_status_code == -1, "exit deve usar codigo -1 em falha de setsockopt")) return 0;
    if (!assert_true(setsockopt_called == 1, "setsockopt deve ter sido chamado")) return 0;

    return 1;
}

static int test_configura_timeout_socket_success(void) {
    reset_fakes();

    int rc = configura_timeout_socket(9, 2500);

    if (!assert_true(rc == 0, "configura_timeout_socket deve retornar 0 em sucesso")) return 0;
    if (!assert_true(setsockopt_called == 1, "setsockopt deve ser chamado para timeout")) return 0;
    if (!assert_true(last_setsockopt_socket == 9, "setsockopt deve usar o socket informado")) return 0;
    if (!assert_true(last_setsockopt_level == SOL_SOCKET, "timeout deve usar SOL_SOCKET")) return 0;
    if (!assert_true(last_setsockopt_optname == SO_RCVTIMEO, "timeout deve usar SO_RCVTIMEO")) return 0;
    if (!assert_true(last_timeval.tv_sec == 2, "2500ms deve resultar em 2 segundos")) return 0;
    if (!assert_true(last_timeval.tv_usec == 500000, "2500ms deve resultar em 500000 microssegundos")) return 0;

    return 1;
}

static int test_configura_timeout_socket_failure(void) {
    reset_fakes();
    fake_setsockopt_return = -1;

    int rc = configura_timeout_socket(9, 100);

    if (!assert_true(rc == -1, "configura_timeout_socket deve retornar -1 em falha")) return 0;
    if (!assert_true(setsockopt_called == 1, "setsockopt deve ser chamado em timeout")) return 0;

    return 1;
}

static int test_fecha_raw_socket_success(void) {
    reset_fakes();

    int rc = fecha_raw_socket(15);

    if (!assert_true(rc == 0, "fecha_raw_socket deve retornar 0 em sucesso")) return 0;
    if (!assert_true(close_called == 1, "close deve ser chamado uma vez")) return 0;
    if (!assert_true(last_close_socket == 15, "close deve usar o descritor informado")) return 0;

    return 1;
}

static int test_fecha_raw_socket_failure(void) {
    reset_fakes();
    fake_close_return = -1;

    int rc = fecha_raw_socket(15);

    if (!assert_true(rc == -1, "fecha_raw_socket deve retornar -1 em falha")) return 0;
    if (!assert_true(close_called == 1, "close deve ser chamado mesmo em falha")) return 0;

    return 1;
}

int main(void) {
    int passed = 0;
    int total = 0;

    total++; passed += test_cria_raw_socket_success();
    total++; passed += test_cria_raw_socket_socket_failure();
    total++; passed += test_cria_raw_socket_bind_failure();
    total++; passed += test_cria_raw_socket_setsockopt_failure();
    total++; passed += test_configura_timeout_socket_success();
    total++; passed += test_configura_timeout_socket_failure();
    total++; passed += test_fecha_raw_socket_success();
    total++; passed += test_fecha_raw_socket_failure();

    if (passed != total) {
        fprintf(stderr, "%d/%d testes unitarios passaram\n", passed, total);
        return 1;
    }

    printf("%d/%d testes unitarios passaram\n", passed, total);
    return 0;
}
