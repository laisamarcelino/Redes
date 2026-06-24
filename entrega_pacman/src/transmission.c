#define _POSIX_C_SOURCE 200809L

#include "transmission.h"
#include "network.h"
#include "protocol.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SUCESSO 0
#define ERRO -1

/* Retorna o rótulo de log do processo atual (CLI ou SRV) */
static const char *label_proprio(void)
{
    return log_get_contexto();
}

/* Retorna o rótulo de log do lado remoto da comunicação */
static const char *label_outro(void)
{
    return (log_get_contexto()[0] == 'S') ? "CLI" : "SRV";
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* Envia uma confirmação (ACK) ou rejeição (NACK) para o número de sequência informado */
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
    // ACK e NACK não carregam payload para manter o controle pequeno e direto.
    resposta.tamanho_dados = 0;

    if (monta_pacote(&resposta, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar resposta de controle\n");
        return ERRO;
    }

    ssize_t enviado = envia_mensagem(soquete, pacote, tamanho_pacote);

    if (enviado < 0)
    {
        perror("envia_mensagem resposta controle");
        return ERRO;
    }

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

/* Aguarda indefinidamente um ACK ou NACK com o número de sequência esperado */
int espera_ack_nack(int *p_soquete, uint8_t sequencia_esperada)
{
    uint8_t pacote_resposta[TAMANHO_MAX_PACOTE];
    mensagem_t resposta;

    while (1)
    {
        ssize_t recebido = espera_mensagem_servidor(
            p_soquete,
            pacote_resposta,
            sizeof(pacote_resposta));

        if (recebido < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("espera_mensagem_servidor");
            return ERRO;
        }

        if (recebido == 0)
        {
            continue;
        }

        if (desmonta_pacote(
                pacote_resposta,
                (size_t)recebido,
                &resposta) != 0)
        {
            fprintf(stderr, "[ERRO] Cliente recebeu pacote invalido\n");
            return ERRO;
        }

        // Respostas atrasadas de outra sequência são ignoradas para evitar confirmar pacote errado.
        if (resposta.num_sequencia_msg != sequencia_esperada)
        {
            continue;
        }

        if (resposta.tipo_msg == MSG_ACK)
        {
            log_mensagem(label_outro(), &resposta);
            return MSG_ACK;
        }

        if (resposta.tipo_msg == MSG_NACK)
        {
            log_mensagem(label_outro(), &resposta);
            return MSG_NACK;
        }

        continue;
    }
}
/* Envia um pacote e o reenvia automaticamente até receber ACK ou esgotar as tentativas */
int envia_pacote_com_reenvio(int *p_soquete, mensagem_t *mensagem, uint8_t *proxima_sequencia)
{
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    if (mensagem == NULL || proxima_sequencia == NULL)
    {
        fprintf(stderr, "[ERRO] Parametro nulo em envia_pacote_com_reenvio\n");
        return ERRO;
    }

    // Reenvios preservam a sequência para o receptor reconhecer duplicatas.
    mensagem->num_sequencia_msg = *proxima_sequencia;

    if (monta_pacote(mensagem, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return ERRO;
    }

    // Queda de cabo fica fora do limite; só NACK e timeout contam como falha de entrega.
    int tentativa_normal = 1;
    while (1)
    {
        uint8_t pacote_envio[TAMANHO_MAX_PACOTE];
        memcpy(pacote_envio, pacote, tamanho_pacote);

        if (tentativa_normal == 1)
            log_mensagem(label_proprio(), mensagem);
        else
            log_evento("ERRO %s reenvio seq=%02u tentativa %d/%d",
                       label_proprio(), mensagem->num_sequencia_msg,
                       tentativa_normal, MAX_TENTATIVAS_REENVIO);

        ssize_t enviado = envia_mensagem(*p_soquete, pacote_envio, tamanho_pacote);

        if (enviado < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == ENETDOWN  || errno == ENETUNREACH ||
                errno == ENXIO     || errno == ENOBUFS)
            {
                aguarda_link_e_recria_socket(p_soquete);
                log_evento("REDE %s reconectado, reenviando seq=%02u",
                           label_proprio(), mensagem->num_sequencia_msg);
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

        int resposta = espera_ack_nack(p_soquete, mensagem->num_sequencia_msg);

        if (resposta == MSG_ACK)
        {
            *proxima_sequencia = calcula_proxima_sequencia(*proxima_sequencia);
            return SUCESSO;
        }

        if (resposta == MSG_NACK)
        {
            log_evento("ERRO %s NACK seq=%02u, reenviando (%d/%d)",
                       label_outro(), mensagem->num_sequencia_msg,
                       tentativa_normal, MAX_TENTATIVAS_REENVIO);
            tentativa_normal++;
            if (tentativa_normal > MAX_TENTATIVAS_REENVIO)
            {
                fprintf(stderr, "[ERRO] %d tentativas esgotadas para seq=%u\n",
                        MAX_TENTATIVAS_REENVIO, mensagem->num_sequencia_msg);
                return ERRO;
            }
            continue;
        }

        fprintf(stderr, "[ERRO] Falha inesperada esperando ACK/NACK\n");
        return ERRO;
    }
}

/* Divide o buffer em blocos e os envia sequencialmente, encerrando com MSG_FIM_TRANSMISSAO */
int envia_buffer_protocolado(int *p_soquete, uint8_t tipo_msg, const uint8_t *buffer,
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
                p_soquete,
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
            p_soquete,
            &fim,
            proxima_sequencia) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao enviar MSG_FIM_TRANSMISSAO\n");
        return ERRO;
    }

    log_evento("transmissao em blocos finalizada");

    return SUCESSO;
}

/* Avança o número de sequência circular para o próximo valor */
uint8_t calcula_proxima_sequencia(uint8_t sequencia)
{
    return (uint8_t)((sequencia + 1) % (SEQUENCIA_MAX + 1));
}

/* Retorna o número de sequência imediatamente anterior ao valor informado */
uint8_t calcula_sequencia_anterior(uint8_t sequencia)
{
    return (sequencia == 0) ? SEQUENCIA_MAX : (uint8_t)(sequencia - 1);
}
/* Lê o número de sequência diretamente dos bytes do pacote, sem desmontá-lo completamente */
uint8_t extrai_sequencia_pacote_bruto(const uint8_t *pacote)
{
    if (pacote == NULL)
    {
        return SUCESSO;
    }

    return (uint8_t)(((pacote[1] & 0x07) << 3) |
                     ((pacote[2] >> 5) & 0x07));
}
