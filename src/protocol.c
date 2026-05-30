#include "protocol.h"

#define SUCESSO 0
#define ERRO -1

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

#define CRC_TEMPORARIO 0x00

static int tipo_msg_valido(uint8_t tipo_msg)
{
    switch (tipo_msg)
    {
    case MSG_ACK:
    case MSG_NACK:
    case MSG_VISUALIZACAO:
    case MSG_INICIALIZACAO:
    case MSG_DADOS:
    case MSG_TXT:
    case MSG_JPG:
    case MSG_MP4:
    case MSG_MOV_DIREITA:
    case MSG_MOV_ESQUERDA:
    case MSG_MOV_CIMA:
    case MSG_MOV_BAIXO:
    case MSG_ERRO:
    case MSG_FIM_TRANSMISSAO:
        return 1;

    default:
        return 0;
    }
}

static void extrai_campos_pacote(
    const uint8_t *pacote,
    uint8_t *tamanho_dados,
    uint8_t *num_sequencia_msg,
    uint8_t *tipo_msg)
{
    /* APAGAR
     * pacote[1]:
     * bits 7..3 = tamanho_dados
     */
    *tamanho_dados = (pacote[1] >> 3) & 0x1F;

    /* APAGAR
     * Sequencia:
     * - bits 2..0 de pacote[1] viram os 3 bits mais altos
     * - bits 7..5 de pacote[2] viram os 3 bits mais baixos
     */
    *num_sequencia_msg = (uint8_t)(((pacote[1] & 0x07) << 3) |
                                   ((pacote[2] >> 5) & 0x07));

    /* APAGAR
     * pacote[2]:
     * bits 4..0 = tipo_msg
     */
    *tipo_msg = pacote[2] & 0x1F;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

int monta_pacote(const mensagem_t *mensagem, uint8_t pacote[TAMANHO_MAX_PACOTE],
                 size_t *tamanho_pacote)
{
    size_t tamanho_total;

    if (mensagem == NULL)
    {
        fprintf(stderr, "[ERRO] Mensagem nula em monta_pacote\n");
        return ERRO;
    }

    if (pacote == NULL)
    {
        fprintf(stderr, "[ERRO] Buffer de pacote nulo em monta_pacote\n");
        return ERRO;
    }

    if (tamanho_pacote == NULL)
    {
        fprintf(stderr, "[ERRO] Ponteiro tamanho_pacote nulo em monta_pacote\n");
        return ERRO;
    }

    if (mensagem->tipo_msg > TIPO_MAX || !tipo_msg_valido(mensagem->tipo_msg))
    {
        fprintf(stderr, "[ERRO] Tipo de mensagem invalido: %u\n",
                mensagem->tipo_msg);
        return ERRO;
    }

    if (mensagem->num_sequencia_msg > SEQUENCIA_MAX)
    {
        fprintf(stderr, "[ERRO] Numero de sequencia invalido: %u\n",
                mensagem->num_sequencia_msg);
        return ERRO;
    }

    if (mensagem->tamanho_dados > TAMANHO_MAX_DADOS)
    {
        fprintf(stderr, "[ERRO] Tamanho de dados invalido: %u\n",
                mensagem->tamanho_dados);
        return ERRO;
    }

    tamanho_total = TAMANHO_CABECALHO_PROTOCOLO + mensagem->tamanho_dados + TAMANHO_CRC_PROTOCOLO;

    /* APAGAR
     * Isso evita lixo de memoria nos bytes nao usados.
     */
    // Limpa o pacote
    memset(pacote, 0, TAMANHO_MAX_PACOTE);

    /* APAGAR
     * pacote[0]:
     * marcador de inicio
     */
    pacote[0] = MARCADOR_INICIO;

    /* APAGAR
     * pacote[1]:
     *
     * bits 7..3 = tamanho_dados
     * bits 2..0 = 3 bits mais altos da sequencia
     *
     * Exemplo visual:
     *   T T T T T S S S
     */
    // 5 bits de tamanho e 3 de sequencia
    pacote[1] = (uint8_t)(((mensagem->tamanho_dados & 0x1F) << 3) |
                          ((mensagem->num_sequencia_msg >> 3) & 0x07));

    /* APAGAR
     * pacote[2]:
     *
     * bits 7..5 = 3 bits mais baixos da sequencia
     * bits 4..0 = tipo_msg
     *
     * Exemplo visual:
     *   S S S T T T T T
     */
    // 3 bits de sequencia e 5 de tipo
    pacote[2] = (uint8_t)(((mensagem->num_sequencia_msg & 0x07) << 5) |
                          (mensagem->tipo_msg & 0x1F));

    // Dados começam no pacote[3]
    // Copia os dados da msg para dentro do pacote
    if (mensagem->tamanho_dados > 0)
    {
        memcpy(
            pacote + TAMANHO_CABECALHO_PROTOCOLO,
            mensagem->dados,
            mensagem->tamanho_dados);
    }

    // DEBUG - Implementar CRC
    // CRC estará no ultimo byte do pacote
    pacote[tamanho_total - 1] = CRC_TEMPORARIO;

    *tamanho_pacote = tamanho_total;

    return SUCESSO;
}

int valida_pacote(const uint8_t *pacote, size_t tamanho_pacote)
{
    uint8_t tamanho_dados;
    uint8_t num_sequencia_msg;
    uint8_t tipo_msg;
    size_t tamanho_esperado;

    if (pacote == NULL)
    {
        fprintf(stderr, "[ERRO] Pacote nulo em valida_pacote\n");
        return ERRO;
    }

    // Menor pacote possível: 3 bytes de cabeçalho + 0 de dados + 1 de CRC
    if (tamanho_pacote < TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_CRC_PROTOCOLO)
    {
        fprintf(stderr,
                "[ERRO] Pacote pequeno demais: %zu bytes\n",
                tamanho_pacote);
        return ERRO;
    }

    // Maior pacote possível: 3 bytes de cabeçalho + 31 de dados + 1 de CRC = 35 bytes
    if (tamanho_pacote > TAMANHO_MAX_PACOTE)
    {
        fprintf(stderr,
                "[ERRO] Pacote grande demais: %zu bytes\n",
                tamanho_pacote);
        return ERRO;
    }

    /* APAGAR
     * pacote[0] deve ser sempre o marcador de início.
     */
    if (pacote[0] != MARCADOR_INICIO)
    {
        fprintf(stderr,
                "[ERRO] Marcador de inicio invalido. Recebido: 0x%02X, esperado: 0x%02X\n",
                pacote[0],
                MARCADOR_INICIO);
        return ERRO;
    }

    extrai_campos_pacote(
        pacote,
        &tamanho_dados,
        &num_sequencia_msg,
        &tipo_msg);

    if (tamanho_dados > TAMANHO_MAX_DADOS)
    {
        fprintf(stderr,
                "[ERRO] Tamanho de dados invalido: %u\n",
                tamanho_dados);
        return ERRO;
    }

    if (num_sequencia_msg > SEQUENCIA_MAX)
    {
        fprintf(stderr,
                "[ERRO] Numero de sequencia invalido: %u\n",
                num_sequencia_msg);
        return ERRO;
    }

    if (tipo_msg > TIPO_MAX)
    {
        fprintf(stderr,
                "[ERRO] Tipo de mensagem invalido: %u\n",
                tipo_msg);
        return ERRO;
    }

    if (!tipo_msg_valido(tipo_msg))
    {
        fprintf(stderr,
                "[ERRO] Tipo de mensagem desconhecido: %u\n",
                tipo_msg);
        return ERRO;
    }

    // Confere se o tamanho recebido bate com o tamanho informado no cabecalho
    tamanho_esperado = TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados + TAMANHO_CRC_PROTOCOLO;

    if (tamanho_pacote != tamanho_esperado)
    {
        fprintf(stderr,
                "[ERRO] Tamanho do pacote inconsistente. Recebido: %zu, esperado: %zu\n",
                tamanho_pacote,
                tamanho_esperado);
        return ERRO;
    }

    /* DEBUG
     * CRC ainda nao esta implementado.
     * Por enquanto, apenas garantimos que o byte reservado existe,
     * pois ele ja foi considerado no tamanho_esperado.
     */

    return SUCESSO;
}

int desmonta_pacote(const uint8_t *pacote, size_t tamanho_pacote,
                    mensagem_t *mensagem)
{
    uint8_t tamanho_dados;
    uint8_t num_sequencia_msg;
    uint8_t tipo_msg;

    if (mensagem == NULL)
    {
        fprintf(stderr, "[ERRO] Mensagem nula em desmonta_pacote\n");
        return ERRO;
    }

    if (valida_pacote(pacote, tamanho_pacote) != SUCESSO)
    {
        fprintf(stderr, "[ERRO] Pacote invalido em desmonta_pacote\n");
        return ERRO;
    }

    extrai_campos_pacote(
        pacote,
        &tamanho_dados,
        &num_sequencia_msg,
        &tipo_msg);

    // Limpa a mensagem antes de preencher
    memset(mensagem, 0, sizeof(mensagem_t));

    mensagem->tipo_msg = tipo_msg;
    mensagem->num_sequencia_msg = num_sequencia_msg;
    mensagem->tamanho_dados = tamanho_dados;

    // Dados depois do cabeçalho
    if (tamanho_dados > 0)
    {
        memcpy(
            mensagem->dados,
            &pacote[TAMANHO_CABECALHO_PROTOCOLO],
            tamanho_dados);
    }

    return SUCESSO;
}

void imprime_pacote(const uint8_t *pacote, size_t tamanho_pacote)
{
    if (pacote == NULL)
    {
        fprintf(stderr, "[ERRO] Pacote nulo em imprime_pacote\n");
        return;
    }

    printf("[DEBUG] Tamanho do pacote: %zu bytes\n", tamanho_pacote);

    for (size_t i = 0; i < tamanho_pacote; i++)
    {
        printf("[DEBUG] pacote[%zu] = 0x%02X\n", i, pacote[i]);
    }
}
