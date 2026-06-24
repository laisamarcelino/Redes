# PacMan Remoto com Raw Socket

## Descrição

Este projeto implementa um jogo de **PacMan “no escuro”** no modelo **cliente-servidor**, utilizando **C** e **raw socket**.

O cliente é responsável apenas pela interação com o usuário: ler comandos, enviar movimentos ao servidor, receber respostas e mostrar a visualização atual do jogo.

O servidor mantém o estado completo da partida: carrega o mapa, processa o movimento do PacMan, move os fantasmas, gera a visualização parcial e envia arquivos correspondentes às pastilhas coletadas ou a eventos do jogo.

Além da lógica do jogo, o trabalho exige a implementação de um **protocolo próprio de comunicação**, incluindo:

- envio e recepção por raw socket;
- validação de mensagens;
- CRC;
- ACK/NACK;
- timeout;
- retransmissão;
- controle de fluxo **para-e-espera**.

---

## Objetivo

O objetivo do projeto é construir uma aplicação cliente-servidor sobre raw socket, implementando um protocolo confiável e usando esse protocolo para suportar o funcionamento do jogo PacMan remoto.

---

## Estrutura do projeto

```text
pacman-raw/
├── README.md
├── Makefile
├── maps/
│   └── default.csv
├── assets/
│   ├── 1.txt
│   ├── 2.txt
│   ├── 3.jpg
│   ├── 4.jpg
│   ├── 5.mp4
│   ├── 6.mp4
│   └── ghost_hit.txt
├── include/
│   ├── common.h
│   ├── protocol.h
│   ├── network.h
│   ├── game.h
│   ├── client.h
│   └── server.h
├── src/
│   ├── common.c
│   ├── protocol.c
│   ├── network.c
│   ├── game.c
│   ├── client.c
│   ├── server.c
│   ├── client_main.c
│   └── server_main.c
├── logs/
├── tests/
└── docs/
    └── relatorio.pdf
```
