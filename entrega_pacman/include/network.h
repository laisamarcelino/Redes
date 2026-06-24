#ifndef NETWORK_H
#define NETWORK_H

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

#define TAM_BUFFER_RAW 1600
#define REDE_TIMEOUT -2

char *seleciona_interface_rede(int allow_loopback);
int cria_raw_socket(char *nome_interface_rede);
ssize_t espera_mensagem_servidor(int *p_soquete, unsigned char *buffer, size_t tamanho_buffer);
ssize_t envia_mensagem(int soquete, const unsigned char *buffer, size_t tamanho_buffer);
int fecha_raw_socket(int soquete);
ssize_t espera_mensagem_timeout(int *p_soquete, unsigned char *buffer,
                                size_t tamanho_buffer, int timeout_ms);
int aguarda_link_e_recria_socket(int *p_soquete);

#endif
