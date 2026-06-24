#include "protocol.h"

#define SUCESSO 0
#define ERRO -1

/* ===================================================================
                         FUNÇÕES AUXILIARES
======================================================================*/

#define CRC_TEMPORARIO 0x00

/* Retorna verdadeiro se o tipo numérico pertence ao conjunto de mensagens do protocolo PacMan */
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
    case MSG_FIM_JOGO:
    case MSG_ERRO:
    case MSG_FIM_TRANSMISSAO:
        return 1;

    default:
        return 0;
    }
}

/* Decodifica os campos compactados do cabeçalho de um pacote bruto */
static void extrai_campos_pacote(
    const uint8_t *pacote,
    uint8_t *tamanho_dados,
    uint8_t *num_sequencia_msg,
    uint8_t *tipo_msg)
{
    *tamanho_dados = (pacote[1] >> 3) & 0x1F;
    *num_sequencia_msg = (uint8_t)(((pacote[1] & 0x07) << 3) |
                                   ((pacote[2] >> 5) & 0x07));
    *tipo_msg = pacote[2] & 0x1F;
}

/* Calcula o CRC-8 dos bytes fornecidos para detecção de erros de transmissão */
static uint8_t calcula_crc8(const uint8_t *dados, size_t tamanho)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < tamanho; i++)
    {
        crc ^= dados[i];
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)((crc << 1) ^ 0x07);
            }
            else
            {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ===================================================================
                         FUNÇÕES PRINCIPAIS
======================================================================*/

/* Serializa uma mensagem em bytes com cabeçalho e CRC, pronta para envio pela rede */
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
    memset(pacote, 0, TAMANHO_MAX_PACOTE);
    pacote[0] = MARCADOR_INICIO;

    // O cabeçalho foi compactado em 3 bytes para respeitar o formato definido no projeto.
    pacote[1] = (uint8_t)(((mensagem->tamanho_dados & 0x1F) << 3) |
                          ((mensagem->num_sequencia_msg >> 3) & 0x07));
    pacote[2] = (uint8_t)(((mensagem->num_sequencia_msg & 0x07) << 5) |
                          (mensagem->tipo_msg & 0x1F));
    if (mensagem->tamanho_dados > 0)
    {
        memcpy(
            pacote + TAMANHO_CABECALHO_PROTOCOLO,
            mensagem->dados,
            mensagem->tamanho_dados);
    }
    pacote[tamanho_total - 1] = calcula_crc8(
        pacote,
        TAMANHO_CABECALHO_PROTOCOLO + mensagem->tamanho_dados);

    *tamanho_pacote = tamanho_total;

    return SUCESSO;
}

/* Verifica o marcador de início, os campos do cabeçalho e a integridade do CRC do pacote */
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

    if (tamanho_pacote < TAMANHO_CABECALHO_PROTOCOLO + TAMANHO_CRC_PROTOCOLO)
    {
        fprintf(stderr,
                "[ERRO] Pacote pequeno demais: %zu bytes\n",
                tamanho_pacote);
        return ERRO;
    }

    if (tamanho_pacote > TAMANHO_MAX_PACOTE)
    {
        fprintf(stderr,
                "[ERRO] Pacote grande demais: %zu bytes\n",
                tamanho_pacote);
        return ERRO;
    }
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

    tamanho_esperado = TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados + TAMANHO_CRC_PROTOCOLO;

    if (tamanho_pacote < tamanho_esperado)
    {
        fprintf(stderr,
                "[ERRO] Pacote incompleto. Recebido: %zu, esperado no minimo: %zu\n",
                tamanho_pacote,
                tamanho_esperado);
        return ERRO;
    }

    // O CRC usa o tamanho lógico do pacote porque o Ethernet pode completar com padding.
    uint8_t crc_recebido = pacote[tamanho_esperado - 1];

    uint8_t crc_calculado = calcula_crc8(
        pacote,
        TAMANHO_CABECALHO_PROTOCOLO + tamanho_dados);

    if (crc_recebido != crc_calculado)
    {
        fprintf(stderr,
                "[ERRO] CRC invalido. Recebido: 0x%02X, calculado: 0x%02X\n",
                crc_recebido,
                crc_calculado);
        return ERRO;
    }

    return SUCESSO;
}

/* Valida e converte um pacote de bytes de volta para a estrutura de mensagem */
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

    memset(mensagem, 0, sizeof(mensagem_t));

    mensagem->tipo_msg = tipo_msg;
    mensagem->num_sequencia_msg = num_sequencia_msg;
    mensagem->tamanho_dados = tamanho_dados;

    if (tamanho_dados > 0)
    {
        memcpy(
            mensagem->dados,
            &pacote[TAMANHO_CABECALHO_PROTOCOLO],
            tamanho_dados);
    }

    return SUCESSO;
}

/* Exibe no terminal cada byte do pacote em formato hexadecimal para depuração */
void imprime_pacote(const uint8_t *pacote, size_t tamanho_pacote)
{
    if (pacote == NULL)
    {
        fprintf(stderr, "[ERRO] Pacote nulo em imprime_pacote\n");
        return;
    }

    printf("Tamanho do pacote: %zu bytes\n", tamanho_pacote);

    for (size_t i = 0; i < tamanho_pacote; i++)
    {
        printf("pacote[%zu] = 0x%02X\n", i, pacote[i]);
    }
}
