#include "protocol.h"

int main(void)
{
    mensagem_t msg_enviada;
    mensagem_t msg_recebida;

    uint8_t pacote[TAMANHO_MAX_PACOTE];
    size_t tamanho_pacote;

    memset(&msg_enviada, 0, sizeof(mensagem_t));
    memset(&msg_recebida, 0, sizeof(mensagem_t));

    msg_enviada.tipo_msg = MSG_DADOS;
    msg_enviada.num_sequencia_msg = 5;
    msg_enviada.tamanho_dados = 2;
    msg_enviada.dados[0] = 'o';
    msg_enviada.dados[1] = 'i';

    if (monta_pacote(&msg_enviada, pacote, &tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao montar pacote\n");
        return 1;
    }

    printf("\n[DEBUG] Pacote montado:\n");
    imprime_pacote(pacote, tamanho_pacote);

    if (valida_pacote(pacote, tamanho_pacote) != 0)
    {
        fprintf(stderr, "[ERRO] Pacote invalido\n");
        return 1;
    }

    if (desmonta_pacote(pacote, tamanho_pacote, &msg_recebida) != 0)
    {
        fprintf(stderr, "[ERRO] Falha ao desmontar pacote\n");
        return 1;
    }

    printf("\n[DEBUG] Mensagem desmontada:\n");
    printf("[DEBUG] Tipo: %u\n", msg_recebida.tipo_msg);
    printf("[DEBUG] Sequencia: %u\n", msg_recebida.num_sequencia_msg);
    printf("[DEBUG] Tamanho dados: %u\n", msg_recebida.tamanho_dados);

    printf("[DEBUG] Dados: ");
    for (int i = 0; i < msg_recebida.tamanho_dados; i++)
    {
        printf("%c", msg_recebida.dados[i]);
    }
    printf("\n");

    return 0;
}
