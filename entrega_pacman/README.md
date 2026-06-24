# PacMan Remoto

## Compilar

No diretório do projeto, execute:

```bash
make
```

O executável gerado será:

```bash
./pacman
```

Para limpar a compilação:

```bash
make clean
```

## Executar

O programa usa `raw socket`, então deve ser executado com `sudo`.

Em uma máquina, inicie o servidor:

```bash
sudo ./pacman -s
```

Em outra máquina, inicie o cliente:

```bash
sudo ./pacman -c jogar
```

No cliente, use:

```text
w = mover para cima
s = mover para baixo
a = mover para esquerda
d = mover para direita
q = sair
```

## Usar outro mapa

Por padrão, o servidor usa o mapa padrão do projeto.

Para escolher um mapa específico:

```bash
sudo ./pacman -s -m maps/arena.csv
```

ou:

```bash
sudo ./pacman -s -m maps/corredor.csv
```

Os mapas aceitos devem conter apenas:

```text
X = parede
0 = espaço livre
```

PacMan, fantasmas e pastilhas são sorteados automaticamente pelo servidor.

## Escolher interface de rede

O programa tenta escolher automaticamente uma interface Ethernet ativa.

Para escolher manualmente:

```bash
sudo ./pacman -s -i eth0
```

No cliente:

```bash
sudo ./pacman -c jogar -i eth0
```

## Logs

Para ativar logs, use `-v`.

Servidor:

```bash
sudo ./pacman -s -v
```

Cliente:

```bash
sudo ./pacman -c jogar -v
```

Os logs são salvos em:

```text
/tmp/pacman_SRV.log
/tmp/pacman_CLI.log
```

Para acompanhar em tempo real:

```bash
tail -f /tmp/pacman_SRV.log /tmp/pacman_CLI.log
```

ou:

```bash
tail -f /tmp/pacman_SRV.log
```

ou:

```bash
tail -f /tmp/pacman_CLI.log
```

## Limpar logs

Para apagar os logs antigos:

```bash
sudo rm -f /tmp/pacman_SRV.log /tmp/pacman_CLI.log
```

Para apenas esvaziar os arquivos:

```bash
sudo truncate -s 0 /tmp/pacman_SRV.log /tmp/pacman_CLI.log
```
