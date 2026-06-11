#define _POSIX_C_SOURCE 200809L

#include "client.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

static uint8_t proxima_sequencia_cliente = 0;
static uint8_t sequencia_esperada_servidor = 0;

// Envia mensagens grandes, separando-as em blocos
static int envia_buffer_protocolado(
    int soquete,
    uint8_t tipo_msg,
    const uint8_t *buffer,
    size_t tamanho_buffer,
    uint8_t *proxima_sequencia)
{
    // Offset indica o inicio do proximo bloco
    size_t offset = 0;

    if (buffer == NULL && tamanho_buffer > 0)
    {
        fprintf(stderr, "[ERRO] Buffer nulo com tamanho maior que zero\n");
        return -1;
    }

    // Envia todos os blocos de dados
    while (offset < tamanho_buffer)
    {
        // Mensagem temporaria para o bloco atual
        mensagem_t mensagem;
        size_t bytes_restantes = tamanho_buffer - offset;
        uint8_t tamanho_bloco;

        // Limpa a mensagem antes de preencher
        memset(&mensagem, 0, sizeof(mensagem));

        // Limita cada bloco ao tamanho maximo do protocolo
        if (bytes_restantes > TAMANHO_MAX_DADOS)
        {
            tamanho_bloco = TAMANHO_MAX_DADOS;
        }
        else
        {
            tamanho_bloco = (uint8_t)bytes_restantes;
        }

        mensagem.tipo_msg = tipo_msg;
        mensagem.tamanho_dados = tamanho_bloco;

        // Copia o trecho atual do buffer para a mensagem
        memcpy(
            mensagem.dados,
            buffer + offset,
            tamanho_bloco);

        // Envia o bloco com reenvio em caso de falha temporaria
        if (envia_pacote_com_reenvio(
                soquete,
                &mensagem,
                proxima_sequencia) != 0)
        {
            fprintf(stderr,
                    "[ERRO] Falha ao enviar bloco no offset %zu\n",
                    offset);
            return -1;
        }

        // Avanca para o proximo trecho do buffer
        offset += tamanho_bloco;
    }

    /* Depois de enviar todos os blocos, envia um pacote especial
     * informando que a transmissão terminou
     */
    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));

    // Marca o pacote como fim de transmissao
    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    // Envia o pacote final para o servidor imprimir a mensagem completa
    if (envia_pacote_com_reenvio(
            soquete,
            &fim,
            proxima_sequencia) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO\n");
        return -1;
    }

    printf("[DEBUG] Transmissão em blocos finalizada\n");

    return 0;
}

static int tipo_arquivo_recebido(uint8_t tipo_msg)
{
    return tipo_msg == MSG_TXT || tipo_msg == MSG_JPG || tipo_msg == MSG_MP4;
}

static const char *extensao_arquivo_recebido(uint8_t tipo_msg)
{
    if (tipo_msg == MSG_TXT)
    {
        return "txt";
    }

    if (tipo_msg == MSG_JPG)
    {
        return "jpg";
    }

    if (tipo_msg == MSG_MP4)
    {
        return "mp4";
    }

    return "bin";
}

static int envia_comando_jogo(int soquete, uint8_t tipo_msg)
{
    mensagem_t mensagem;

    memset(&mensagem, 0, sizeof(mensagem));
    mensagem.tipo_msg = tipo_msg;
    mensagem.tamanho_dados = 0;

    return envia_pacote_com_reenvio(
        soquete,
        &mensagem,
        &proxima_sequencia_cliente);
}

static int remonta_fragmento_cliente(
    uint8_t **buffer,
    size_t *tamanho_atual,
    size_t *capacidade,
    const uint8_t *dados,
    size_t tamanho_dados)
{
    if (tamanho_dados == 0)
    {
        return 0;
    }

    if (*tamanho_atual + tamanho_dados > *capacidade)
    {
        size_t nova_capacidade = (*capacidade == 0) ? 256 : *capacidade;
        uint8_t *novo_buffer;

        while (*tamanho_atual + tamanho_dados > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        novo_buffer = realloc(*buffer, nova_capacidade);
        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao alocar recepcao do cliente\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    memcpy(*buffer + *tamanho_atual, dados, tamanho_dados);
    *tamanho_atual += tamanho_dados;
    return 0;
}

static int salva_arquivo_cliente(uint8_t tipo_msg, const uint8_t *buffer, size_t tamanho)
{
    static int contador = 1;
    char caminho[64];
    FILE *arquivo;

    snprintf(
        caminho,
        sizeof(caminho),
        "cliente_recebido_%03d.%s",
        contador,
        extensao_arquivo_recebido(tipo_msg));
    contador++;

    arquivo = fopen(caminho, "wb");
    if (arquivo == NULL)
    {
        perror("fopen arquivo cliente");
        return -1;
    }

    if (tamanho > 0 && fwrite(buffer, 1, tamanho, arquivo) != tamanho)
    {
        perror("fwrite arquivo cliente");
        fclose(arquivo);
        return -1;
    }

    fclose(arquivo);

    printf("\n[ARQUIVO] Recebido e salvo em %s (%zu bytes)\n", caminho, tamanho);

    if (tipo_msg == MSG_TXT && buffer != NULL && tamanho > 0)
    {
        printf("[CONTEUDO]\n");
        fwrite(buffer, 1, tamanho, stdout);
        printf("\n");
    }

    return 0;
}

static int buffer_contem_texto(const uint8_t *buffer, size_t tamanho, const char *texto)
{
    size_t tamanho_texto;

    if (buffer == NULL || texto == NULL)
    {
        return 0;
    }

    tamanho_texto = strlen(texto);
    if (tamanho_texto == 0 || tamanho_texto > tamanho)
    {
        return 0;
    }

    for (size_t i = 0; i + tamanho_texto <= tamanho; i++)
    {
        if (memcmp(buffer + i, texto, tamanho_texto) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static int recebe_transmissao_servidor(
    int soquete,
    int timeout_primeiro_pacote_ms,
    uint8_t *tipo_transmissao,
    uint8_t **buffer_saida,
    size_t *tamanho_saida)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;
    uint8_t *buffer = NULL;
    size_t tamanho = 0;
    size_t capacidade = 0;
    uint8_t tipo_atual = MSG_DADOS;

    if (tipo_transmissao == NULL || buffer_saida == NULL || tamanho_saida == NULL)
    {
        return -1;
    }

    *buffer_saida = NULL;
    *tamanho_saida = 0;
    *tipo_transmissao = MSG_DADOS;

    while (1)
    {
        ssize_t recebido;

        if (timeout_primeiro_pacote_ms > 0 && tamanho == 0)
        {
            recebido = espera_mensagem_timeout(
                soquete,
                pacote,
                sizeof(pacote),
                timeout_primeiro_pacote_ms);

            if (recebido == REDE_TIMEOUT)
            {
                return REDE_TIMEOUT;
            }
        }
        else
        {
            recebido = espera_mensagem_servidor(
                soquete,
                pacote,
                sizeof(pacote));
        }

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("recebe_transmissao_servidor");
            free(buffer);
            return -1;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                envia_ack_nack(
                    soquete,
                    MSG_NACK,
                    extrai_sequencia_pacote_bruto(pacote));
            }

            continue;
        }

        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada_servidor))
        {
            envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.num_sequencia_msg != sequencia_esperada_servidor)
        {
            envia_ack_nack(soquete, MSG_NACK, mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
            sequencia_esperada_servidor =
                calcula_proxima_sequencia(sequencia_esperada_servidor);

            *tipo_transmissao = tipo_atual;
            *buffer_saida = buffer;
            *tamanho_saida = tamanho;
            return 0;
        }

        if (mensagem.tipo_msg == MSG_VISUALIZACAO ||
            tipo_arquivo_recebido(mensagem.tipo_msg))
        {
            tipo_atual = mensagem.tipo_msg;

            if (remonta_fragmento_cliente(
                    &buffer,
                    &tamanho,
                    &capacidade,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(buffer);
                return -1;
            }

            envia_ack_nack(soquete, MSG_ACK, mensagem.num_sequencia_msg);
            sequencia_esperada_servidor =
                calcula_proxima_sequencia(sequencia_esperada_servidor);
            continue;
        }

        envia_ack_nack(soquete, MSG_NACK, mensagem.num_sequencia_msg);
    }
}

static int mostra_resposta_servidor(int soquete, int espera_obrigatoria)
{
    uint8_t tipo;
    uint8_t *buffer = NULL;
    size_t tamanho = 0;
    int timeout = espera_obrigatoria ? 0 : 500;
    int resultado = recebe_transmissao_servidor(
        soquete,
        timeout,
        &tipo,
        &buffer,
        &tamanho);

    if (resultado == REDE_TIMEOUT)
    {
        return 0;
    }

    if (resultado != 0)
    {
        return -1;
    }

    if (tipo == MSG_VISUALIZACAO)
    {
        printf("\n");
        if (buffer != NULL && tamanho > 0)
        {
            fwrite(buffer, 1, tamanho, stdout);
        }

        if (buffer_contem_texto(buffer, tamanho, "Resultado: VITORIA") ||
            buffer_contem_texto(buffer, tamanho, "Resultado: DERROTA"))
        {
            free(buffer);
            return 1;
        }
    }
    else if (tipo_arquivo_recebido(tipo))
    {
        if (salva_arquivo_cliente(tipo, buffer, tamanho) != 0)
        {
            free(buffer);
            return -1;
        }
    }

    free(buffer);
    return 0;
}

static uint8_t movimento_por_entrada(char entrada)
{
    switch (entrada)
    {
    case 'w':
    case 'W':
        return MSG_MOV_CIMA;
    case 'd':
    case 'D':
        return MSG_MOV_DIREITA;
    case 's':
    case 'S':
        return MSG_MOV_BAIXO;
    case 'a':
    case 'A':
        return MSG_MOV_ESQUERDA;
    default:
        return MSG_ERRO;
    }
}

static int executa_cliente_jogo(int soquete)
{
    char linha[32];

    if (envia_comando_jogo(soquete, MSG_INICIALIZACAO) != 0)
    {
        return -1;
    }

    if (mostra_resposta_servidor(soquete, 1) != 0)
    {
        return -1;
    }

    while (1)
    {
        uint8_t movimento;

        printf("\nMovimento (W/A/S/D, Q para sair): ");
        fflush(stdout);

        if (fgets(linha, sizeof(linha), stdin) == NULL)
        {
            return 0;
        }

        if (linha[0] == 'q' || linha[0] == 'Q')
        {
            return 0;
        }

        movimento = movimento_por_entrada(linha[0]);
        if (movimento == MSG_ERRO)
        {
            printf("Movimento invalido. Use W/A/S/D.\n");
            continue;
        }

        if (envia_comando_jogo(soquete, movimento) != 0)
        {
            return -1;
        }

        int estado_visualizacao = mostra_resposta_servidor(soquete, 1);

        if (estado_visualizacao < 0)
        {
            return -1;
        }

        /*
         * Depois da visualizacao, o servidor pode mandar um arquivo de premio
         * ou de colisao. Se nao vier nada rapidamente, seguimos para a rodada.
         */
        if (mostra_resposta_servidor(soquete, 0) != 0)
        {
            return -1;
        }

        if (estado_visualizacao > 0)
        {
            return 0;
        }
    }
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

int executa_cliente(int soquete, const char *mensagem)
{
    // Valida a mensagem recebida pela linha de comando
    if (mensagem == NULL || mensagem[0] == '\0')
    {
        fprintf(stderr, "Nenhuma mensagem informada\n");
        return -1;
    }

    if (strcmp(mensagem, "game") == 0 || strcmp(mensagem, "jogo") == 0)
    {
        return executa_cliente_jogo(soquete);
    }

    /*
     * Teste temporario:
     * se o argumento comecar com "arquivo:", envia o arquivo informado.
     *
     * Exemplo:
     *   sudo ./pacman -c "arquivo:premios/1.txt" -l
     */
    if (strncmp(mensagem, "arquivo:", 8) == 0)
    {
        const char *caminho = mensagem + 8;
        uint8_t tipo = tipo_arquivo_por_caminho(caminho);

        if (tipo == MSG_ERRO)
        {
            fprintf(stderr, "[ERRO] Extensao de arquivo nao suportada: %s\n", caminho);
            return -1;
        }

        return envia_arquivo_protocolado(
            soquete,
            caminho,
            tipo,
            &proxima_sequencia_cliente);
    }

    // Envia a mensagem em blocos, trata ACK, NACK e timeout.
    return envia_buffer_protocolado(
        soquete,
        MSG_DADOS,
        (const uint8_t *)mensagem,
        strlen(mensagem),
        &proxima_sequencia_cliente);
}
