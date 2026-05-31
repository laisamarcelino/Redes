#define _POSIX_C_SOURCE 200809L

#include "server.h"
#include "network.h"
#include "protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

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

// Envia uma resposta de controle ao cliente
static int envia_ack_nack(int soquete, uint8_t tipo_resposta, uint8_t sequencia)
{
    mensagem_t resposta;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    if (tipo_resposta != MSG_ACK && tipo_resposta != MSG_NACK)
    {
        fprintf(stderr,
                "[ERRO] Tipo de resposta de controle invalido: %u\n",
                tipo_resposta);
        return -1;
    }

    memset(&resposta, 0, sizeof(resposta));

    resposta.tipo_msg = tipo_resposta;
    resposta.num_sequencia_msg = sequencia;
    resposta.tamanho_dados = 0; // ACK e NACK nao carregam dados

    // Monta o pacote com a mensagem (ack ou nack)
    if (monta_pacote(&resposta, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar resposta de controle\n");
        return -1;
    }

    // Envia o pacote de controle pela camada de rede
    ssize_t enviado = envia_mensagem(soquete, pacote, tamanho_pacote);

    if (enviado < 0)
    {
        perror("envia_mensagem resposta controle");
        return -1;
    }

    // Confere se todos os bytes do pacote foram enviados
    if ((size_t)enviado != tamanho_pacote)
    {
        fprintf(stderr,
                "[ERRO] Envio incompleto da resposta. Enviado: %zd, esperado: %zu\n",
                enviado,
                tamanho_pacote);
        return -1;
    }

    if (tipo_resposta == MSG_ACK)
    {
        printf("[DEBUG] ACK enviado para seq=%u\n", sequencia);
    }
    else
    {
        printf("[DEBUG] NACK enviado para seq=%u\n", sequencia);
    }

    return 0;
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

// Calcula a proxima sequencia respeitando o limite de 6 bits
static uint8_t proxima_sequencia(uint8_t sequencia)
{
    return (uint8_t)((sequencia + 1) % (SEQUENCIA_MAX + 1));
}

// Calcula a sequencia anterior respeitando o limite de 6 bits
static uint8_t sequencia_anterior(uint8_t sequencia)
{
    return (sequencia == 0) ? SEQUENCIA_MAX : (uint8_t)(sequencia - 1);
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
                uint8_t sequencia_erro = (uint8_t)(((pacote[1] & 0x07) << 3) |
                                                   ((pacote[2] >> 5) & 0x07));

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
        if (mensagem.num_sequencia_msg == sequencia_anterior(sequencia_esperada))
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

            sequencia_esperada = proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            continue;
        }

        // Trata o fim da transmissao
        if (mensagem.tipo_msg == MSG_FIM_TRANSMISSAO)
        {
            sequencia_esperada = proxima_sequencia(sequencia_esperada);

            envia_ack_nack(
                soquete,
                MSG_ACK,
                mensagem.num_sequencia_msg);

            imprime_mensagem_completa(buffer_recebido, tamanho_recebido);

            free(buffer_recebido);
            buffer_recebido = NULL;
            tamanho_recebido = 0;
            capacidade_recebido = 0;

            continue;
        }

        // Mostra no terminal mensagens de debug
        imprime_mensagem_protocolada(&mensagem);

        // Avança a sequencia
        sequencia_esperada = proxima_sequencia(sequencia_esperada);

        // Confirma o recebimento da msg
        envia_ack_nack(
            soquete,
            MSG_ACK,
            mensagem.num_sequencia_msg);
    }
}
