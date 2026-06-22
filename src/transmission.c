#define _POSIX_C_SOURCE 200809L

#include "transmission.h"
#include "network.h"
#include "protocol.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define SUCESSO 0
#define ERRO -1

static const char *label_proprio(void)
{
    return log_get_contexto();
}

static const char *label_outro(void)
{
    return (log_get_contexto()[0] == 'S') ? "CLI" : "SRV";
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

// Envia uma resposta de controle ao cliente
int envia_ack_nack(int soquete, uint8_t tipo_resposta, uint8_t sequencia)
{
    mensagem_t resposta;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    if (tipo_resposta != MSG_ACK && tipo_resposta != MSG_NACK)
    {
        fprintf(stderr,
                "[ERRO] Tipo de resposta de controle invalido: %u\n",
                tipo_resposta);
        return ERRO;
    }

    memset(&resposta, 0, sizeof(resposta));

    resposta.tipo_msg = tipo_resposta;
    resposta.num_sequencia_msg = sequencia;
    resposta.tamanho_dados = 0; // ACK e NACK nao carregam dados

    // Monta o pacote com a mensagem (ack ou nack)
    if (monta_pacote(&resposta, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar resposta de controle\n");
        return ERRO;
    }

    // Envia o pacote de controle pela camada de rede
    ssize_t enviado = envia_mensagem(soquete, pacote, tamanho_pacote);

    if (enviado < 0)
    {
        perror("envia_mensagem resposta controle");
        return ERRO;
    }

    // Confere se todos os bytes do pacote foram enviados
    if ((size_t)enviado != tamanho_pacote)
    {
        fprintf(stderr,
                "[ERRO] Envio incompleto da resposta. Enviado: %zd, esperado: %zu\n",
                enviado,
                tamanho_pacote);
        return ERRO;
    }

    mensagem_t log_ctrl;
    log_ctrl.tipo_msg          = tipo_resposta;
    log_ctrl.num_sequencia_msg = sequencia;
    log_ctrl.tamanho_dados     = 0;
    log_mensagem(label_proprio(), &log_ctrl);

    return SUCESSO;
}

int espera_ack_nack_com_timeout(int soquete, uint8_t sequencia_esperada)
{
    // Buffer que recebe o pacote de resposta
    uint8_t pacote_resposta[TAMANHO_MAX_PACOTE];
    mensagem_t resposta;

    while (1)
    {
        // Aguarda uma resposta respeitando o limite de tempo
        ssize_t recebido = espera_mensagem_timeout(
            soquete,
            pacote_resposta,
            sizeof(pacote_resposta),
            TIMEOUT_ACK_MS);

        // Se nao tiver resposta dentro do prazo, avisa o chamador
        if (recebido == REDE_TIMEOUT)
        {
            fprintf(stderr,
                    "[DEBUG] Timeout esperando ACK/NACK seq=%u\n",
                    sequencia_esperada);
            return REDE_TIMEOUT;
        }

        // Interrupcoes por sinal nao encerram a espera
        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_timeout");
            return ERRO;
        }

        // Ignora leituras vazias
        if (recebido == 0)
        {
            continue;
        }

        // Converte o pacote recebido em mensagem
        if (desmonta_pacote(
                pacote_resposta,
                (size_t)recebido,
                &resposta) != 0)
        {
            fprintf(stderr, "[ERRO] Cliente recebeu pacote invalido\n");
            return ERRO;
        }

        // Ignora resposta de outra sequencia
        if (resposta.num_sequencia_msg != sequencia_esperada)
        {
            fprintf(stderr,
                    "[DEBUG] Resposta ignorada. Esperado seq=%u, recebido seq=%u\n",
                    sequencia_esperada,
                    resposta.num_sequencia_msg);
            continue;
        }

        // ACK confirma o pacote enviado
        if (resposta.tipo_msg == MSG_ACK)
        {
            log_mensagem(label_outro(), &resposta);
            return MSG_ACK;
        }

        // NACK pede reenvio do pacote
        if (resposta.tipo_msg == MSG_NACK)
        {
            log_mensagem(label_outro(), &resposta);
            return MSG_NACK;
        }

        // Outros tipos sao ignorados enquanto o prazo nao acaba
        fprintf(stderr,
                "[DEBUG] Tipo de resposta ignorado: %u\n",
                resposta.tipo_msg);
    }
}

/* APAGAR
 *
 * Fluxo para-e-espera
 * envia pacote
 * espera ACK/NACK
 * se ACK: confirma
 * se NACK: reenvia
 * se timeout: reenvia
 */
// Envia uma unica mensagem de tamanho maximo 31 bytes
int envia_pacote_com_reenvio(int soquete, mensagem_t *mensagem, uint8_t *proxima_sequencia)
{
    // Buffer do pacote montado pelo protocolo
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    /* APAGAR
     * A sequência é definida aqui.
     * Em caso de reenvio, a mesma sequência é mantida.
     */
    if (mensagem == NULL || proxima_sequencia == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro nulo em envia_pacote_com_reenvio\n");
        return ERRO;
    }

    // Define a sequência atual; reenvios mantem este mesmo numero
    mensagem->num_sequencia_msg = *proxima_sequencia;

    if (monta_pacote(mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return ERRO;
    }

    // Reenvia indefinidamente ate receber ACK ou ocorrer erro fatal
    int tentativa = 1;
    while (1)
    {
        uint8_t pacote_envio[TAMANHO_MAX_PACOTE];
        memcpy(pacote_envio, pacote, tamanho_pacote);

        if (tentativa == 1)
            log_mensagem(label_proprio(), mensagem);
        else
            log_evento("ERRO %s reenvio seq=%02u tentativa %d",
                       label_proprio(), mensagem->num_sequencia_msg, tentativa);

        ssize_t enviado = envia_mensagem(soquete, pacote_envio, tamanho_pacote);

        if (enviado < 0)
        {
            if (errno == EINTR   || errno == ENETDOWN  ||
                errno == ENETUNREACH || errno == ENXIO ||
                errno == ENOBUFS)
            {
                tentativa++;
                continue;
            }

            perror("envia_mensagem");
            return ERRO;
        }

        if ((size_t)enviado != tamanho_pacote)
        {
            fprintf(stderr,
                    "[ERRO] Envio incompleto. Enviado: %zd, esperado: %zu\n",
                    enviado, tamanho_pacote);
            return ERRO;
        }

        int resposta = espera_ack_nack_com_timeout(soquete, mensagem->num_sequencia_msg);

        if (resposta == MSG_ACK)
        {
            *proxima_sequencia = calcula_proxima_sequencia(*proxima_sequencia);
            return SUCESSO;
        }

        if (resposta == MSG_NACK)
        {
            log_evento("ERRO %s NACK seq=%02u, reenviando",
                       label_outro(), mensagem->num_sequencia_msg);
            tentativa++;
            continue;
        }

        if (resposta == REDE_TIMEOUT)
        {
            log_evento("ERRO timeout seq=%02u, reenviando", mensagem->num_sequencia_msg);
            tentativa++;
            continue;
        }

        fprintf(stderr, "[ERRO] Falha inesperada esperando ACK/NACK\n");
        return ERRO;
    }
}

// Envia mensagens grandes, separando-as em blocos do tamanho maximo do protocolo
int envia_buffer_protocolado(int soquete, uint8_t tipo_msg, const uint8_t *buffer,
                             size_t tamanho_buffer, uint8_t *proxima_sequencia)
{
    size_t offset = 0;

    if (buffer == NULL && tamanho_buffer > 0)
    {
        fprintf(stderr, "[ERRO] Buffer nulo com tamanho maior que zero\n");
        return ERRO;
    }

    if (proxima_sequencia == NULL)
    {
        fprintf(stderr, "[ERRO] Sequencia nula em envia_buffer_protocolado\n");
        return ERRO;
    }

    while (offset < tamanho_buffer)
    {
        mensagem_t mensagem;
        size_t bytes_restantes = tamanho_buffer - offset;
        uint8_t tamanho_bloco;

        memset(&mensagem, 0, sizeof(mensagem));

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

        memcpy(
            mensagem.dados,
            buffer + offset,
            tamanho_bloco);

        if (envia_pacote_com_reenvio(
                soquete,
                &mensagem,
                proxima_sequencia) != 0)
        {
            fprintf(stderr,
                    "[ERRO] Falha ao enviar bloco no offset %zu\n",
                    offset);
            return ERRO;
        }

        offset += tamanho_bloco;
    }

    mensagem_t fim;
    memset(&fim, 0, sizeof(fim));
    fim.tipo_msg = MSG_FIM_TRANSMISSAO;
    fim.tamanho_dados = 0;

    if (envia_pacote_com_reenvio(
            soquete,
            &fim,
            proxima_sequencia) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO\n");
        return ERRO;
    }

    log_evento("transmissao em blocos finalizada");

    return SUCESSO;
}

// Calcula a proxima sequencia respeitando o limite de 6 bits
uint8_t calcula_proxima_sequencia(uint8_t sequencia)
{
    return (uint8_t)((sequencia + 1) % (SEQUENCIA_MAX + 1));
}

// Calcula a sequencia anterior respeitando o limite de 6 bits
uint8_t calcula_sequencia_anterior(uint8_t sequencia)
{
    return (sequencia == 0) ? SEQUENCIA_MAX : (uint8_t)(sequencia - 1);
}

/* APAGAR
 *  Se o pacote falhou na validação, mas possui cabeçalho mínimo
 * e marcador de início correto, ainda conseguimos extrair a sequência
 * para enviar NACK.
 *
 * Isso cobre, por exemplo, pacotes com CRC inválido.
 */
// Se falhar, o servidor manda NACK, se conseguir extrair a sequência do cabeçalho
uint8_t extrai_sequencia_pacote_bruto(const uint8_t *pacote)
{
    if (pacote == NULL)
    {
        return SUCESSO;
    }

    return (uint8_t)(((pacote[1] & 0x07) << 3) |
                     ((pacote[2] >> 5) & 0x07));
}
