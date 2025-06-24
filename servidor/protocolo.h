#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <stdint.h>

#define MAX_DADOS 127
#define MARCADOR_INICIO 0x7E
#define TAMANHO_FRAME (6 + MAX_DADOS) // header + payload
#define ERRO_SEM_PERMISSAO 0
#define ERRO_ESPACO_INSUFICIENTE 1

typedef unsigned char uchar;

typedef struct {
    uchar marcador_inicio; // 0x7E
    uchar tamanho;         // até 127
    uchar sequencia;       // 5 bits
    uchar tipo;            // 4 bits
    uchar checksum;        // XOR de tudo acima + dados
    uchar dados[MAX_DADOS];
} Frame;

typedef struct {
    int x;
    int y;
} Posicao;

long long timestamp_ms();

// Funções de frame
Frame criar_frame(uchar sequencia, uchar tipo, uchar *dados, uchar tamanho);
uchar calcular_checksum(Frame *frame);
int verificar_checksum(Frame *frame);
void print_frame(Frame *frame);

// Funções de rede
int enviar_frame(int socket_fd, const Frame *frame, const uchar *dest_mac);
int receber_frame(int socket_fd, Frame *frame, const uchar *filtro_mac);
int cria_raw_socket(char* nome_interface_rede);

// Stop-and-wait: envio e recepção com controle de fluxo
int enviar_com_ack(int sock, const Frame *frame, const uchar *dest_mac, int timeout_ms);
int receber_com_ack(int sock, Frame *frame, uchar *mac_origem, int timeout_ms);

#endif