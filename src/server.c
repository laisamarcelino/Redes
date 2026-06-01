#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"
#include "files.h"
#include "transmission.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/
uint8_t sequencia_esperada = 0;

// Retorna verdadeiro quando o tipo recebido representa um bloco de arquivo.
static int tipo_arquivo(uint8_t tipo_msg)
{
    return tipo_msg == MSG_TXT || tipo_msg == MSG_JPG || tipo_msg == MSG_MP4;
}

// Escolhe o nome local usado pelo servidor para salvar o arquivo recebido.
static const char *caminho_saida_arquivo(uint8_t tipo_msg)
{
    if (tipo_msg == MSG_TXT)
    {
        return "recebido.txt";
    }

    if (tipo_msg == MSG_JPG)
    {
        return "recebido.jpg";
    }

    if (tipo_msg == MSG_MP4)
    {
        return "recebido.mp4";
    }

    return NULL;
}

// Função usada para debug
static void imprime_mensagem_protocolada(const mensagem_t *mensagem)
{
    if (mensagem == NULL)
    {
        return;
    }

    printf("[DEBUG] Tipo: %u\n", mensagem->tipo_msg);
    printf("[DEBUG] Sequencia: %u\n", mensagem->num_sequencia_msg);
    printf("[DEBUG] Tamanho dados: %u\n", mensagem->tamanho_dados);

    // Imprime o payload como texto quando possivel
    printf("[DEBUG] Dados: ");
    for (uint8_t i = 0; i < mensagem->tamanho_dados; i++)
    {
        unsigned char c = mensagem->dados[i];

        // Caracteres imprimiveis aparecem como texto normal
        if (c >= 32 && c <= 126)
        {
            putchar(c);
        }
        else
        {
            // Bytes nao imprimiveis aparecem em hexadecimal
            printf("\\x%02X", c);
        }
    }
    putchar('\n');
    // Garante que a saida apareça mesmo com o servidor em loop
    fflush(stdout);
}


// Responsavel por remontar msgs fragmentadas (protocolo permite 31 bytes por msg)
static int remonta_mensagem(uint8_t **buffer, size_t *tamanho_atual,
                            size_t *capacidade, const uint8_t *dados,
                            size_t tamanho_dados)
{
    // Fragmento vazio nao altera a mensagem remontada
    if (tamanho_dados == 0)
    {
        return 0;
    }

    // Aumenta o buffer quando o fragmento nao cabe
    if (*tamanho_atual + tamanho_dados > *capacidade)
    {
        // Aumenta a capacidade para 128 bytes
        size_t nova_capacidade = (*capacidade == 0) ? 128 : *capacidade;

        // Dobra a capacidade ate caber o novo tamanho
        while (*tamanho_atual + tamanho_dados > nova_capacidade)
        {
            nova_capacidade *= 2;
        }

        // Realoca preservando os dados ja recebidos
        uint8_t *novo_buffer = realloc(*buffer, nova_capacidade);

        if (novo_buffer == NULL)
        {
            fprintf(stderr, "[ERRO] Falha ao realocar buffer de recebimento\n");
            return -1;
        }

        *buffer = novo_buffer;
        *capacidade = nova_capacidade;
    }

    // Copia o fragmento no final da mensagem remontada
    memcpy(
        *buffer + *tamanho_atual,
        dados,
        tamanho_dados);

    // Atualiza o tamanho total ja recebido
    *tamanho_atual += tamanho_dados;

    return 0;
}

// Imprime o buffer completo remontado
static void imprime_mensagem_completa(const uint8_t *buffer, size_t tamanho)
{
    // Imprime a mensagem remontada com todos os fragmentos
    printf("[DEBUG] Mensagem completa recebida com %zu bytes: ", tamanho);

    // Escreve os bytes exatamente como chegaram quando houver conteudo
    if (buffer != NULL && tamanho > 0)
    {
        fwrite(buffer, 1, tamanho, stdout);
    }

    printf("\n");
    fflush(stdout);
}

// Grava o buffer remontado quando a transmissao recebida era de arquivo.
static int salva_arquivo_completo(uint8_t tipo_msg, const uint8_t *buffer, size_t tamanho)
{
    const char *caminho_saida = caminho_saida_arquivo(tipo_msg);

    if (caminho_saida == NULL)
    {
        fprintf(stderr, "[ERRO] Tipo de arquivo sem caminho de saida: %u\n", tipo_msg);
        return -1;
    }

    FILE *arquivo = fopen(caminho_saida, "wb");
    if (arquivo == NULL)
    {
        perror("fopen arquivo recebido");
        return -1;
    }

    // Arquivos binarios precisam ser escritos exatamente byte a byte.
    if (tamanho > 0 && fwrite(buffer, 1, tamanho, arquivo) != tamanho)
    {
        perror("fwrite arquivo recebido");
        fclose(arquivo);
        return -1;
    }

    fclose(arquivo);

    printf("[DEBUG] Arquivo recebido salvo em %s com %zu bytes\n",
           caminho_saida,
           tamanho);

    return 0;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

// Fica em loop infinito esperando pacotes
int executa_servidor(int soquete)
{
    // Buffer que recebe um pacote PacMan ja sem cabecalho Ethernet
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    mensagem_t mensagem;
    uint8_t *buffer_recebido = NULL;
    size_t tamanho_recebido = 0;
    size_t capacidade_recebido = 0;
    uint8_t sequencia_esperada = 0;
    uint8_t tipo_transmissao_atual = MSG_DADOS;

    printf("Servidor aguardando pacotes do protocolo PacMan...\n");

    while (1)
    {
        // Bloqueia ate chegar um pacote PacMan valido na camada de rede
        ssize_t recebido = espera_mensagem_servidor(
            soquete,
            pacote,
            sizeof(pacote));

        // Trata erro de recebimento sem encerrar em interrupcoes de sinal
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return -1;
        }

        // Mensagem vazia não forma msg para o protocolo
        if (recebido == 0)
        {
            continue;
        }

        // Valida e transforma o pacote bruto em uma mensagem do protocolo
        if (desmonta_pacote(pacote, (size_t)recebido, &mensagem) != 0)
        {
            fprintf(stderr, "[ERRO] Pacote invalido recebido\n");

            /* APAGAR
             *  Se o pacote falhou na validação, mas possui cabeçalho mínimo
             * e marcador de início correto, ainda conseguimos extrair a sequência
             * para enviar NACK.
             *
             * Isso cobre, por exemplo, pacotes com CRC inválido.
             */
            // Se falhar, o servidor manda NACK, se conseguir extrair a sequência do cabeçalho
            if (recebido >= TAMANHO_CABECALHO_PROTOCOLO &&
                pacote[0] == MARCADOR_INICIO)
            {
                uint8_t sequencia_erro = extrai_sequencia_pacote_bruto(pacote);

                envia_ack_nack(
                    soquete,
                    MSG_NACK,
                    sequencia_erro);
            }

            continue;
        }

        /* APAGAR
         * Se o servidor já aceitou esse pacote antes ele nao reprocessa o pacote
         * de novo, mas reenvia ACK. Isso evita duplicar os dados
         */
        // Reenvio da ultima sequencia ja aceita recebe ACK sem duplicar dados
        if (mensagem.num_sequencia_msg ==
            calcula_sequencia_anterior(sequencia_esperada))
        {
            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        // Sequencia fora de ordem recebe NACK para pedir reenvio
        if (mensagem.num_sequencia_msg != sequencia_esperada)
        {
            fprintf(stderr,
                    "[ERRO] Sequencia inesperada. Recebido: %u, esperado: %u\n",
                    mensagem.num_sequencia_msg,
                    sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_NACK,
                mensagem.num_sequencia_msg);
            continue;
        }

        // Fragmentos de dados sao acumulados ate o fim da transmissao
        if (mensagem.tipo_msg == MSG_DADOS)
        {
            if (remonta_mensagem(
                    &buffer_recebido,
                    &tamanho_recebido,
                    &capacidade_recebido,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        // Trata o fim da transmissao
        // Fragmentos de arquivo tambem sao acumulados ate o fim da transmissao.
        if (tipo_arquivo(mensagem.tipo_msg))
        {
            tipo_transmissao_atual = mensagem.tipo_msg;

            if (remonta_mensagem(
                    &buffer_recebido,
                    &tamanho_recebido,
                    &capacidade_recebido,
                    mensagem.dados,
                    mensagem.tamanho_dados) != 0)
            {
                free(buffer_recebido);
                return -1;
            }

            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            if (tipo_arquivo(tipo_transmissao_atual))
            {
                if (salva_arquivo_completo(
                        tipo_transmissao_atual,
                        buffer_recebido,
                        tamanho_recebido) != 0)
                {
                    free(buffer_recebido);
                    return -1;
                }
            }
            else
            {
                imprime_mensagem_completa(buffer_recebido, tamanho_recebido);
            }

            free(buffer_recebido);
            buffer_recebido = NULL;
            tamanho_recebido = 0;
            capacidade_recebido = 0;
            tipo_transmissao_atual = MSG_DADOS;

            continue;
        }

        // Mostra no terminal mensagens de debug
        imprime_mensagem_protocolada(&mensagem);

        // Avança a sequencia
        sequencia_esperada = calcula_proxima_sequencia(sequencia_esperada);

        // Confirma o recebimento da msg
        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);
    }
}
