#ifndef SERVER_H
#define SERVER_H

// Executa o modo servidor. caminho_mapa pode ser NULL para usar o mapa padrao.
int executa_servidor(int soquete, const char *caminho_mapa);

#endif