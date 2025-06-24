#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocolo.h"
#include <unistd.h>
#include <sys/statvfs.h>

#define INTERFACE "enp0s31f6"                             // interface
#define TIMEOUT_ACK 2000                                  // 2 segundos
#define MAC_SERVIDOR {0xff, 0xff, 0xff, 0xff, 0xff, 0xff} // broadcast
#define VAZIO 0
#define PERCORRIDO 1
#define TESOURO 2
#define JOGADOR 3

// enum para estados da celula do grid
typedef enum
{
    CELULA_VAZIA,
    CELULA_PERCORRIDA,
    CELULA_TESOURO_COLETADO,
    CELULA_JOGADOR
} EstadoCelula;

// struct para cada celula do grid
typedef struct
{
    EstadoCelula estado;
    int tem_tesouro;
} Celula;

// grid
Celula grid[8][8] = {{0}};

// posicao atual do jogador
Posicao pos_atual = {0, 0};
// mac do servidor
uchar mac_servidor[6] = MAC_SERVIDOR;
// numero de sequencia para frames
uchar sequencia = 0;

// inicializa todas as celulas como vazias
void inicializa_grid()
{
    for (int i = 0; i < 8; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            grid[i][j] = (Celula){CELULA_VAZIA, 0};
        }
    }
}

// imprime o grid
void imprime_grid()
{
    // limpa a tela
    printf("\033[H\033[J");
    printf("Caça ao Tesouro - Posição: (%d, %d)\n", pos_atual.x, pos_atual.y);

    for (int j = 7; j >= 0; j--)
    {
        printf(" ");
        for (int i = 0; i < 8; i++)
        {
            if (i == pos_atual.x && j == pos_atual.y)
            {
                // jogador na celula atual
                printf("[@]");
            }
            else
            {
                switch (grid[i][j].estado)
                {
                case CELULA_TESOURO_COLETADO:
                    printf("[X]"); // tesouro coletado
                    break;
                case CELULA_PERCORRIDA:
                    printf("[.]"); // ja percorrido
                    break;
                default:
                    printf("[ ]"); // ainda nao passou
                }
            }
        }
        printf("\n");
    }
    printf("Legenda: @=Você, X=Tesouro Coletado, .=Percorrido\n");
}

// marca a celula como percorrida, caso nao tenha tesouro
void marca_percorrido()
{
    if (grid[pos_atual.x][pos_atual.y].estado != CELULA_TESOURO_COLETADO)
    {
        grid[pos_atual.x][pos_atual.y].estado = CELULA_PERCORRIDA;
    }
}

// recebe um arquivo do servidor
void receber_arquivo(int sock, Frame *resposta)
{
    // verifica se tem espaco livre
    struct statvfs st;
    if (statvfs(".", &st) == 0)
    {
        unsigned long long espaco_livre = st.f_bsize * st.f_bavail;
        // no minimo 1MB
        if (espaco_livre < 1048576)
        {
            // se nao tem, envia erro
            uchar codigo_erro = ERRO_ESPACO_INSUFICIENTE;
            Frame erro = criar_frame(resposta->sequencia, 15, &codigo_erro, 1);
            enviar_com_ack(sock, &erro, mac_servidor, TIMEOUT_ACK);
            return;
        }
    }
    else
    {
        perror("Erro ao verificar espaço livre");
    }

    // extrai o nome do arquivo dos dados do frame
    char nome_arquivo[128];
    strncpy(nome_arquivo, (char *)resposta->dados, resposta->tamanho);
    nome_arquivo[resposta->tamanho] = '\0';

    printf("Recebendo arquivo: %s\n", nome_arquivo);
    FILE *f = fopen(nome_arquivo, "wb");
    if (!f)
    {
        perror("Erro ao criar arquivo");
        return;
    }

    // recebe frames ate sinal de fim (tipo = 9)
    Frame dado;
    while (1)
    {
        if (receber_com_ack(sock, &dado, NULL, TIMEOUT_ACK) != 0)
            break;

        if (dado.tipo == 9) // fim do arquivo
        {
            break;
        }
        else if (dado.tipo == 5) // dados
        {
            fwrite(dado.dados, 1, dado.tamanho, f);
        }
    }
    fclose(f);
    printf("Arquivo recebido com sucesso!\n");

    // exibo o conteudo com base no tipo
    if (resposta->tipo == 6)
    {
        // se for texto, usa 'cat'
        printf("Conteúdo do texto:\n");
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "cat %s", nome_arquivo);
        system(cmd);
    }
    else if (resposta->tipo == 8)
    {
        // se for imagem usa 'feh'
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "feh %s &", nome_arquivo);
        system(cmd);
    }
    else if (resposta->tipo == 7)
    {
        // se for video usa 'mpv'
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "mpv %s &", nome_arquivo);
        system(cmd);
    }
    // marca a celula como tesouro coletado
    grid[pos_atual.x][pos_atual.y].estado = CELULA_TESOURO_COLETADO;
    grid[pos_atual.x][pos_atual.y].tem_tesouro = 1;
}

// imprimir erro enviado pelo servidor
void tratar_erro(uchar codigo)
{
    switch (codigo)
    {
    case ERRO_SEM_PERMISSAO:
        printf("Erro: Permissão de acesso negada!\n");
        break;
    case ERRO_ESPACO_INSUFICIENTE:
        printf("Erro: Espaço insuficiente no cliente!\n");
        break;
    default:
        printf("Erro desconhecido: %d\n", codigo);
    }
}

int main()
{
    // cria o raw socket
    int sock = cria_raw_socket(INTERFACE);
    if (sock < 0)
    {
        fprintf(stderr, "Erro ao criar socket raw\n");
        return 1;
    }

    printf("Cliente iniciado. Conectado à interface %s\n", INTERFACE);
    // configura o grid
    inicializa_grid();
    // exibe o grid
    imprime_grid(pos_atual.x, pos_atual.y);

    // var para leitura da entrada
    char comando;
    // loop principal
    while (1)
    {
        comando = getchar();
        while (getchar() != '\n')
            ;

        // sair ao digitar 'q' ou 'Q'
        if (comando == 'q' || comando == 'Q')
            break;

        // cria frame de movimento
        uchar tipo_mov;
        switch (comando)
        {
        case 'w':
        case 'W':
            tipo_mov = 11; // cima
            break;
        case 's':
        case 'S':
            tipo_mov = 12; // baixo
            break;
        case 'a':
        case 'A':
            tipo_mov = 13; // esquerda
            break;
        case 'd':
        case 'D':
            tipo_mov = 10; // direita
            break;
        default:
            printf("Comando inválido!\n");
            continue;
        }

        Frame movimento = criar_frame(sequencia, tipo_mov, NULL, 0);

        // envia o frame para o servidor
        if (enviar_com_ack(sock, &movimento, mac_servidor, TIMEOUT_ACK) == 0)
        {
            // se teve sucesso, entao incrementa a sequencia
            sequencia = (sequencia + 1) % 32; // Atualiza sequência
            // marca a celula
            marca_percorrido();
            // atualiza a posicao (dentro dos limites)
            switch (tipo_mov)
            {
            case 10:
                if (pos_atual.x < 7)
                    pos_atual.x++;
                break;
            case 11:
                if (pos_atual.y < 7)
                    pos_atual.y++;
                break;
            case 12:
                if (pos_atual.y > 0)
                    pos_atual.y--;
                break;
            case 13:
                if (pos_atual.x > 0)
                    pos_atual.x--;
                break;
            }
        }
        else
        {
            printf("Falha na comunicação com o servidor!\n");
        }

        // verifica resposta do servidor
        Frame resposta;
        if (receber_com_ack(sock, &resposta, NULL, 2000) == 0)
        {
            if (resposta.tipo == 15) // caso for erro
            {
                tratar_erro(resposta.dados[0]);
            }
            else if (resposta.tipo >= 6 && resposta.tipo <= 8) // ou arquivo
            {
                receber_arquivo(sock, &resposta);
            }
        }
        // atualiza o grid
        imprime_grid();
    }

    // ao final, fecha o socket e encerra
    close(sock);
    return 0;
}