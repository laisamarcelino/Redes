#define _POSIX_C_SOURCE 200809L

#include "files.h"
#include "protocol.h"
#include "network.h"
#include "transmission.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUCESSO 0
#define ERRO -1

uint8_t tipo_arquivo_por_caminho(const char *caminho)
{
    const char *extensao;

    if (caminho == NULL)
    {
        return MSG_ERRO;
    }

    extensao = strrchr(caminho, '.');

    if (extensao == NULL)
    {
        return MSG_ERRO;
    }

    if (strcmp(extensao, ".txt") == 0)
    {
        return MSG_TXT;
    }

    if (strcmp(extensao, ".jpg") == 0 || strcmp(extensao, ".jpeg") == 0)
    {
        return MSG_JPG;
    }

    if (strcmp(extensao, ".mp4") == 0)
    {
        return MSG_MP4;
    }

    return MSG_ERRO;
}

int envia_arquivo_protocolado(
    int soquete,
    const char *caminho_arquivo,
    uint8_t tipo_msg,
    uint8_t *proxima_sequencia)
{
    FILE *arquivo;
    uint8_t bloco[TAMANHO_MAX_DADOS];

    if (caminho_arquivo == NULL || proxima_sequencia == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro nulo em envia_arquivo_protocolado\n");
        return ERRO;
    }

    if (tipo_msg != MSG_TXT && tipo_msg != MSG_JPG && tipo_msg != MSG_MP4)
    {
        fprintf(stderr,
                "[ERRO] Tipo de arquivo invalido para envio: %u\n",
                tipo_msg);
        return ERRO;
    }

    arquivo = fopen(caminho_arquivo, "rb");

    if (arquivo == NULL)
    {
        perror("fopen arquivo envio");
        return ERRO;
    }

    printf("[DEBUG] Enviando arquivo: %s\n", caminho_arquivo);

    while (1)
    {
        size_t lidos = fread(bloco, 1, sizeof(bloco), arquivo);

        if (lidos > 0)
        {
            mensagem_t mensagem;
            memset(&mensagem, 0, sizeof(mensagem));

            mensagem.tipo_msg = tipo_msg;
            mensagem.tamanho_dados = (uint8_t)lidos;

            memcpy(mensagem.dados, bloco, lidos);

            if (envia_pacote_com_reenvio(
                    soquete,
                    &mensagem,
                    proxima_sequencia) != SUCESSO)
            {
                fprintf(stderr, "[ERRO] Falha ao enviar bloco do arquivo\n");
                fclose(arquivo);
                return ERRO;
            }
        }

        if (lidos < sizeof(bloco))
        {
            if (ferror(arquivo))
            {
                perror("fread");
                fclose(arquivo);
                return ERRO;
            }

            break;
        }
    }

    fclose(arquivo);

    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));

    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    if (envia_pacote_com_reenvio(
            soquete,
            &fim,
            proxima_sequencia) != SUCESSO)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO do arquivo\n");
        return ERRO;
    }

    printf("[DEBUG] Arquivo enviado com sucesso: %s\n", caminho_arquivo);

    return SUCESSO;
}

int recebe_arquivo_protocolado(
    int soquete,
    const char *caminho_saida,
    uint8_t tipo_esperado,
    uint8_t *sequencia_esperada)
{
    FILE *arquivo;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;

    if (caminho_saida == NULL || sequencia_esperada == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro nulo em recebe_arquivo_protocolado\n");
        return ERRO;
    }

    if (tipo_esperado != MSG_TXT && tipo_esperado != MSG_JPG && tipo_esperado != MSG_MP4)
    {
        fprintf(stderr,
                "[ERRO] Tipo esperado invalido para arquivo: %u\n",
                tipo_esperado);
        return ERRO;
    }

    arquivo = fopen(caminho_saida, "wb");

    if (arquivo == NULL)
    {
        perror("fopen arquivo saida");
        return ERRO;
    }

    printf("[DEBUG] Recebendo arquivo em: %s\n", caminho_saida);

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            fclose(arquivo);
            return ERRO;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != SUCESSO)
        {
            fprintf(stderr, "[ERRO] Pacote invalido durante recebimento de arquivo\n");

            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                uint8_t sequencia_erro = extrai_sequencia_pacote_bruto(pacote);
                envia_ack_nack(soquete, MSG_NACK, sequencia_erro);
            }

            continue;
        }

        /*
         * Caso o ACK anterior tenha se perdido, o emissor pode reenviar
         * o último pacote já aceito. Nesse caso respondemos ACK de novo,
         * mas não escrevemos os dados duplicados no arquivo.
         */
        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(*sequencia_esperada))
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.num_sequencia_msg != *sequencia_esperada)
        {
            fprintf(stderr,
                    "[ERRO] Sequencia inesperada no arquivo. Recebido=%u esperado=%u\n",
                    mensagem.num_sequencia_msg,
                    *sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_NACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            *sequencia_esperada =
                calcula_proxima_sequencia(*sequencia_esperada);

            fclose(arquivo);

            printf("[DEBUG] Arquivo recebido com sucesso: %s\n", caminho_saida);

            return SUCESSO;
        }

        if (mensagem.tipo_msg != tipo_esperado)
        {
            fprintf(stderr,
                    "[ERRO] Tipo de arquivo inesperado. Recebido=%u esperado=%u\n",
                    mensagem.tipo_msg,
                    tipo_esperado);

            envia_ack_nack(
                soquete,
                MSG_NACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        if (mensagem.tamanho_dados > 0)
        {
            size_t escritos = fwrite(
                mensagem.dados,
                1,
                mensagem.tamanho_dados,
                arquivo);

            if (escritos != mensagem.tamanho_dados)
            {
                perror("fwrite");
                fclose(arquivo);
                return ERRO;
            }
        }

        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);

        *sequencia_esperada =
            calcula_proxima_sequencia(*sequencia_esperada);
    }
}