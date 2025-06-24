#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "protocolo.h"

#define INTERFACE "enp0s31f6" // interface
#define TIMEOUT_ACK 2000      // 2 segundos

// codigos de erro
#define ERRO_SEM_PERMISSAO 0
#define ERRO_ESPACO_INSUFICIENTE 1
#define ERRO_MOVIMENTO_INVALIDO 2

// struct para os tesouros do mapa
typedef struct
{
    int x, y;
    int coletado;
} Tesouro;

Tesouro tesouros[8];              // lista de 8 tesouros
int jogador_x = 0, jogador_y = 0; // posicao do jogador
uchar sequencia = 0;              // sequencia dos frames

// mostra o grid no servidor, informando onde estao cada tesouro
void mostra_grid_servidor()
{
    printf("\n--- Mapa do Servidor ---\n");
    for (int j = 7; j >= 0; j--)
    {
        printf(" ");
        for (int i = 0; i < 8; i++)
        {
            if (i == jogador_x && j == jogador_y)
            {
                printf("[@]");
            }
            else
            {
                // verifica se tem tesouro nao coletado na celula
                int tem_tesouro = 0;
                for (int k = 0; k < 8; k++)
                {
                    if (tesouros[k].x == i && tesouros[k].y == j && !tesouros[k].coletado)
                    {
                        tem_tesouro = 1;
                        break;
                    }
                }
                printf(tem_tesouro ? "[X]" : "[ ]");
            }
        }
        printf("\n");
    }
    printf("Legenda: @=Jogador, X=Tesouro\n");
}

// posiciona aleatoriamente 8 tesouros no grid
void inicializa_tesouros()
{
    srand(time(NULL));
    int count = 0;

    while (count < 8)
    {
        int x = rand() % 8;
        int y = rand() % 8;
        int repetido = 0;

        // verifica se essa posicao ja tem tesouro "enterrado"
        for (int j = 0; j < count; j++)
        {
            if (tesouros[j].x == x && tesouros[j].y == y)
            {
                repetido = 1;
                break;
            }
        }

        // se nao, "enterra" tesouro
        if (!repetido)
        {
            tesouros[count].x = x;
            tesouros[count].y = y;
            tesouros[count].coletado = 0;
            count++;
        }
    }
}

// envia o arquivo associado ao tesouro encontrado
void envia_arquivo(int sock, int num_tesouro, uchar seq, const uchar *mac_dest)
{
    // verifica permissao de leitura dos objetos
    if (access("objetos", R_OK) != 0)
    {
        uchar codigo_erro = ERRO_SEM_PERMISSAO;
        Frame erro = criar_frame(seq, 15, &codigo_erro, 1);
        enviar_com_ack(sock, &erro, mac_dest, TIMEOUT_ACK);
        return;
    }

    // tipos possiveis
    const char *extensoes[] = {".txt", ".jpg", ".mp4"};
    uchar tipos[] = {6, 8, 7};
    char caminho[128];

    // verifica cada extensao ate encontrar o arquivo
    for (int i = 0; i < 3; i++)
    {
        snprintf(caminho, sizeof(caminho), "objetos/%d%s", num_tesouro + 1, extensoes[i]);
        struct stat st;
        if (stat(caminho, &st) == 0)
        {
            // envia o frame contendo o nome do arquivo
            const char *nome = strrchr(caminho, '/');
            nome = nome ? nome + 1 : caminho;
            Frame f_nome = criar_frame(seq, tipos[i], (uchar *)nome, strlen(nome));
            enviar_com_ack(sock, &f_nome, mac_dest, TIMEOUT_ACK);

            // abre o arquivo
            FILE *f = fopen(caminho, "rb");
            if (!f)
                return;

            // envia o conteudo em "pedacos" de ate 127 bytes
            uchar buffer[127];
            size_t lidos;
            while ((lidos = fread(buffer, 1, sizeof(buffer), f)) > 0)
            {
                Frame f_dados = criar_frame(seq, 5, buffer, lidos);
                enviar_com_ack(sock, &f_dados, mac_dest, TIMEOUT_ACK);
            }

            // envia o frame de fim de arquivo (tipo = 9)
            Frame f_fim = criar_frame(seq, 9, NULL, 0);
            enviar_com_ack(sock, &f_fim, mac_dest, TIMEOUT_ACK);
            fclose(f);

            // marca o tesouro como coletado
            tesouros[num_tesouro].coletado = 1;
            break;
        }
    }
}

// retorna o indice do tesouro na posicao x,y se existir E nao coletado
// caso contrario, -1
int verifica_tesouro(int x, int y)
{
    for (int i = 0; i < 8; i++)
    {
        if (!tesouros[i].coletado &&
            tesouros[i].x == x &&
            tesouros[i].y == y)
        {
            return i;
        }
    }
    return -1;
}

// exibe a posicao do jogador e os status dos tesouros
void mostra_status()
{
    printf("\n--- Status ---\n");
    printf("Jogador: (%d, %d)\n", jogador_x, jogador_y);
    printf("Tesouros:\n");
    for (int i = 0; i < 8; i++)
    {
        printf("  %d: (%d, %d) %s\n", i + 1, tesouros[i].x, tesouros[i].y,
               tesouros[i].coletado ? "[Coletado]" : "[Disponível]");
    }
    printf("--------------\n");
}

int main()
{
    // cria o raw socket
    int sock = cria_raw_socket(INTERFACE);
    // inicializa os tesouros
    inicializa_tesouros();

    printf("Servidor iniciado. Aguardando movimentos...\n");
    mostra_status();

    // loop principal, processa os frames recebidos do cliente
    while (1)
    {
        Frame recebido;
        uchar mac_cliente[6];

        if (receber_com_ack(sock, &recebido, mac_cliente, TIMEOUT_ACK) == 0)
        {
            // processa o movimento
            int movimento_valido = 1;
            switch (recebido.tipo)
            {
            case 10: // direita
                if (jogador_x >= 7)
                {
                    movimento_valido = 0;
                }
                else
                {
                    jogador_x++;
                }
                break;
            case 11: // cima
                if (jogador_y >= 7)
                {
                    movimento_valido = 0;
                }
                else
                {
                    jogador_y++;
                }
                break;
            case 12: // baixo
                if (jogador_y <= 0)
                {
                    movimento_valido = 0;
                }
                else
                {
                    jogador_y--;
                }
                break;
            case 13: // esquerda
                if (jogador_x <= 0)
                {
                    movimento_valido = 0;
                }
                else
                {
                    jogador_x--;
                }
                break;
            }

            // se o movimento for para fora do grid, retorna erro
            if (!movimento_valido)
            {
                uchar codigo_erro = ERRO_MOVIMENTO_INVALIDO;
                Frame erro = criar_frame(recebido.sequencia, 15, &codigo_erro, 1);
                enviar_com_ack(sock, &erro, mac_cliente, TIMEOUT_ACK);
                continue;
            }

            // atualiza o mapa e os status
            mostra_grid_servidor();
            mostra_status();

            // se "encontrar" o tesou, envia o arquivo
            int idx_tesouro = verifica_tesouro(jogador_x, jogador_y);
            if (idx_tesouro != -1)
            {
                envia_arquivo(sock, idx_tesouro, recebido.sequencia, mac_cliente);
            }
            else
            {
                // caso contrario, envia ACK
                Frame ack = criar_frame(recebido.sequencia, 0, NULL, 0);
                enviar_com_ack(sock, &ack, mac_cliente, TIMEOUT_ACK);
            }
        }
    }

    // ao final, fecha o socket e sai
    close(sock);
    return 0;
}
