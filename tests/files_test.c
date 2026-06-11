#include "files.h"
#include "protocol.h"

#include <stdio.h>

int main(void)
{
    // Testa o mapeamento de extensoes para os tipos usados no protocolo.
    if (tipo_arquivo_por_caminho("mensagem.txt") != MSG_TXT)
    {
        fprintf(stderr, "[ERRO] .txt deveria ser MSG_TXT\n");
        return 1;
    }

    // Testa a extensao principal de imagem.
    if (tipo_arquivo_por_caminho("foto.jpg") != MSG_JPG)
    {
        fprintf(stderr, "[ERRO] .jpg deveria ser MSG_JPG\n");
        return 1;
    }

    // Testa a extensao alternativa de imagem.
    if (tipo_arquivo_por_caminho("foto.jpeg") != MSG_JPG)
    {
        fprintf(stderr, "[ERRO] .jpeg deveria ser MSG_JPG\n");
        return 1;
    }

    // Testa a extensao de video.
    if (tipo_arquivo_por_caminho("video.mp4") != MSG_MP4)
    {
        fprintf(stderr, "[ERRO] .mp4 deveria ser MSG_MP4\n");
        return 1;
    }

    // Extensoes desconhecidas devem ser recusadas.
    if (tipo_arquivo_por_caminho("arquivo.bin") != MSG_ERRO)
    {
        fprintf(stderr, "[ERRO] extensao desconhecida deveria ser MSG_ERRO\n");
        return 1;
    }

    printf("[OK] Tipos de arquivo reconhecidos corretamente\n");
    return 0;
}
