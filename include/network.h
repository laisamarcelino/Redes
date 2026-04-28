#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* EtherType experimental para protocolo próprio (IEEE 802.1Q experimental). */
#define ETHER_TYPE_PROTOCOLO_PROPRIO 0x88B5

/* Cabeçalho simples de mensagem. */
typedef struct {
    uint32_t tamanho_payload;
} msg_cabecalho_t;

/* ===== Declarações de Funções ===== */

int cria_raw_socket(char* nome_interface_rede);
int configura_timeout_socket(int soquete, int timeout_ms);
int fecha_raw_socket(int soquete);

int escolhe_interface_disponivel(char *destino, size_t tamanho);
int obtem_ifindex_interface(const char *nome_interface_rede);
int obtem_mac_interface(const char *nome_interface_rede, unsigned char mac[ETH_ALEN]);
int configura_endereco_destino_raw(int ifindex, const unsigned char *mac_destino,
                                   struct sockaddr_ll *endereco_destino);
ssize_t envia_bytes_raw(int soquete, const void *dados, size_t tamanho_dados,
                       const struct sockaddr_ll *endereco_destino);
ssize_t recebe_bytes_raw(int soquete, void *buffer, size_t tamanho_buffer,
                        struct sockaddr_ll *endereco_origem);

/* Envia mensagem com cabeçalho de tamanho. */
ssize_t envia_mensagem(int soquete, const void *dados, size_t tamanho,
                      const struct sockaddr_ll *endereco_destino);

/* Recebe mensagem com cabeçalho de tamanho. */
ssize_t recebe_mensagem(int soquete, void *buffer, size_t tamanho_max,
                       struct sockaddr_ll *endereco_origem);

#endif