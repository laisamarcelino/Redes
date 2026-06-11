#include <stdio.h>

int main(void)
{
    /*
     * Teste manual de envio de arquivos pelo raw socket.
     *
     * Este teste fica comentado porque depende de permissao de root e de uma
     * interface de rede real ou loopback. Execute em dois terminais.
     *
     * Terminal 1 - servidor usando loopback:
     *   sudo ./pacman -s -l
     *
     * Terminal 2 - cliente enviando texto:
     *   sudo ./pacman -c "arquivo:tests/exemplo.txt" -l
     *
     * Terminal 2 - cliente enviando imagem:
     *   sudo ./pacman -c "arquivo:tests/exemplo.jpg" -l
     *
     * Terminal 2 - cliente enviando video:
     *   sudo ./pacman -c "arquivo:tests/exemplo.mp4" -l
     *
     * Saidas esperadas no servidor:
     *   recebido.txt
     *   recebido.jpg
     *   recebido.mp4
     */

    printf("[INFO] Teste manual documentado nos comentarios deste arquivo\n");
    return 0;
}
