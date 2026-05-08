#ifndef NETWORK_H
#define NETWORK_H

#include <arpa/inet.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <string.h>

// Seleciona a interface de rede (cabo Ethernet)
// Retorna: nome da interface ou NULL se nenhuma encontrada
// allow_loopback: 1 para permitir loopback em testes, 0 para ignorar
char* seleciona_interface_rede(int allow_loopback);
int cria_raw_socket(char* nome_interface_rede);
ssize_t espera_mensagem_servidor(int soquete, unsigned char *buffer, size_t tamanho_buffer);
ssize_t envia_mensagem(int soquete, const unsigned char *buffer, size_t tamanho_buffer);
ssize_t responde_cliente_ok(int soquete);
int fecha_raw_socket(int soquete);

#endif