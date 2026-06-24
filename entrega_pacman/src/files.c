#define _POSIX_C_SOURCE 200809L

#include "files.h"
#include "protocol.h"
#include "transmission.h"

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUCESSO 0
#define ERRO -1

/* Compara duas extensões de arquivo sem distinção entre maiúsculas e minúsculas */
static int extensao_igual(const char *extensao, const char *esperada)
{
    while (*extensao != '\0' && *esperada != '\0')
    {
        unsigned char a = (unsigned char)*extensao;
        unsigned char b = (unsigned char)*esperada;

        if (tolower(a) != tolower(b))
        {
            return 0;
        }

        extensao++;
        esperada++;
    }

    return *extensao == '\0' && *esperada == '\0';
}

/* Retorna verdadeiro se o caminho não começa com '/', ou seja, é relativo ao diretório atual */
static int caminho_relativo(const char *caminho)
{
    return caminho != NULL && caminho[0] != '/';
}

/* Abre o arquivo para leitura; se for relativo e não encontrado, tenta a partir do diretório do executável */
static FILE *abre_arquivo_entrada(const char *caminho_arquivo)
{
    FILE *arquivo = fopen(caminho_arquivo, "rb");

    if (arquivo != NULL || !caminho_relativo(caminho_arquivo))
    {
        return arquivo;
    }

    char caminho_executavel[4096];
    ssize_t tamanho = readlink("/proc/self/exe",
                              caminho_executavel,
                              sizeof(caminho_executavel) - 1);

    if (tamanho <= 0)
    {
        return NULL;
    }

    caminho_executavel[tamanho] = '\0';

    char *ultima_barra = strrchr(caminho_executavel, '/');
    if (ultima_barra == NULL)
    {
        return NULL;
    }

    ultima_barra[1] = '\0';

    char caminho_completo[4096];
    int escritos = snprintf(caminho_completo,
                            sizeof(caminho_completo),
                            "%s%s",
                            caminho_executavel,
                            caminho_arquivo);

    if (escritos < 0 || (size_t)escritos >= sizeof(caminho_completo))
    {
        errno = ENAMETOOLONG;
        return NULL;
    }

    return fopen(caminho_completo, "rb");
}

/* Identifica o tipo de mensagem a ser usado com base na extensão do arquivo */
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

    if (extensao_igual(extensao, ".txt"))
    {
        return MSG_TXT;
    }

    if (extensao_igual(extensao, ".jpg") || extensao_igual(extensao, ".jpeg"))
    {
        return MSG_JPG;
    }

    if (extensao_igual(extensao, ".mp4"))
    {
        return MSG_MP4;
    }

    return MSG_ERRO;
}

/* Lê o arquivo em blocos e os envia via protocolo stop-and-wait, encerrando com FIM_TRANSMISSAO */
int envia_arquivo_protocolado(
    int *p_soquete,
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

    arquivo = abre_arquivo_entrada(caminho_arquivo);

    if (arquivo == NULL)
    {
        fprintf(stderr, "[ERRO] Falha ao abrir arquivo para envio: %s\n",
                caminho_arquivo);
        perror("fopen arquivo envio");
        return ERRO;
    }

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
                    p_soquete,
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

    if (fclose(arquivo) != 0)
    {
        perror("fclose arquivo envio");
        return ERRO;
    }

    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));

    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    if (envia_pacote_com_reenvio(
            p_soquete,
            &fim,
            proxima_sequencia) != SUCESSO)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO do arquivo\n");
        return ERRO;
    }

    return SUCESSO;
}
