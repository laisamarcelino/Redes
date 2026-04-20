#include "include/network.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

static int escolhe_interface(char *destino, size_t tamanho) {
    DIR *diretorio = opendir("/sys/class/net");
    struct dirent *entrada;

    if (diretorio == NULL) {
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
    return -1;
}

int main(void) {
    char interface[IF_NAMESIZE] = {0};

    if (geteuid() != 0) {
        printf("SKIP: teste de integracao exige root ou CAP_NET_RAW\n");
        return 0;
    }

    if (escolhe_interface(interface, sizeof(interface)) != 0) {
        fprintf(stderr, "Falha ao localizar interface de rede para o teste\n");
        return 1;
    }

    int soquete = cria_raw_socket(interface);
    if (soquete < 0) {
        fprintf(stderr, "Falha ao criar raw socket na interface %s\n", interface);
        return 1;
    }

    if (configura_timeout_socket(soquete, 1500) != 0) {
        fprintf(stderr, "Falha ao configurar timeout no raw socket\n");
        fecha_raw_socket(soquete);
        return 1;
    }

    struct timeval timeout = {0};
    socklen_t tamanho_timeout = sizeof(timeout);
    if (getsockopt(soquete, SOL_SOCKET, SO_RCVTIMEO, &timeout, &tamanho_timeout) == -1) {
        fprintf(stderr, "Falha ao ler timeout configurado: %s\n", strerror(errno));
        fecha_raw_socket(soquete);
        return 1;
    }

    if (timeout.tv_sec != 1 || timeout.tv_usec != 500000) {
        fprintf(stderr, "Timeout inesperado: %ld.%06ld\n",
                (long)timeout.tv_sec, (long)timeout.tv_usec);
        fecha_raw_socket(soquete);
        return 1;
    }

    if (fecha_raw_socket(soquete) != 0) {
        fprintf(stderr, "Falha ao fechar raw socket\n");
        return 1;
    }

    printf("Teste de integracao do raw socket passou na interface %s\n", interface);
    return 0;
}
