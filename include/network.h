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

int cria_raw_socket(char* nome_interface_rede);
int configura_timeout_socket(int soquete, int timeout_ms);
int fecha_raw_socket(int soquete);

#endif