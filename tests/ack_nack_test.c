#include "protocol.h"

int main(void)
{
    mensagem_t msg;
    mensagem_t recebida;
    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    memset(&msg, 0, sizeof(msg));

    // Alternar entre ack e nack
    msg.tipo_msg = MSG_NACK;
    msg.num_sequencia_msg = 7;
    msg.tamanho_dados = 0;

    if (monta_pacote(&msg, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar\n");
        return 1;
    }

    if (desmonta_pacote(pacote, tamanho_pacote, &recebida) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao desmontar\n");
        return 1;
    }

    printf("tipo=%u seq=%u tam=%u\n",
           recebida.tipo_msg,
           recebida.num_sequencia_msg,
           recebida.tamanho_dados);

    return 0;
}