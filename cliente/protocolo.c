#include "protocolo.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#define ETHERTYPE_CUSTOM 0x88B5 // exemplo de tipo para identificar o protocolo
#define TAMANHO_ETH 14          // cabecalho Ethernet
#define TIMEOUT_ACK 2000        // 2 segundos

// cria o frame
Frame criar_frame(uchar sequencia, uchar tipo, uchar *dados, uchar tamanho)
{
    Frame frame;
    frame.marcador_inicio = MARCADOR_INICIO;
    frame.tamanho = tamanho;
    frame.sequencia = sequencia & 0x1F; // 5 bits
    frame.tipo = tipo & 0x0F;           // 4 bits

    // copia payload se houver
    if (tamanho > 0 && dados != NULL)
    {
        memcpy(frame.dados, dados, tamanho);
    }

    // adiciona o checksum ao frame
    frame.checksum = calcular_checksum(&frame);
    return frame;
}

// calcula o checksum sobre os campos tamanho, sequencia, tipo e dados
uchar calcular_checksum(Frame *frame)
{
    uchar chk = 0;
    chk ^= frame->tamanho;
    chk ^= frame->sequencia;
    chk ^= frame->tipo;
    for (int i = 0; i < frame->tamanho; i++)
    {
        chk ^= frame->dados[i];
    }
    return chk;
}

// verifica se o checksum bate
int verificar_checksum(Frame *frame)
{
    return calcular_checksum(frame) == frame->checksum;
}

// imprime os campos da frame (USADO PARA DEBUG APENAS)
void print_frame(Frame *frame)
{
    printf("---- Frame ----\n");
    printf("Inicio: 0x%02X\n", frame->marcador_inicio);
    printf("Tamanho: %d\n", frame->tamanho);
    printf("Sequencia: %d\n", frame->sequencia);
    printf("Tipo: %d\n", frame->tipo);
    printf("Checksum: 0x%02X (%s)\n", frame->checksum,
           verificar_checksum(frame) ? "OK" : "ERRO");

    printf("Dados: ");
    for (int i = 0; i < frame->tamanho; i++)
    {
        printf("%02X ", frame->dados[i]);
    }
    printf("\n----------------\n");
}

// monta e envia um frame
int enviar_frame(int socket_fd, const Frame *frame, const uchar *dest_mac)
{
    uchar buffer[1514] = {0};

    // monta o cabecalho
    struct ether_header *eth = (struct ether_header *)buffer;
    memcpy(eth->ether_dhost, dest_mac, 6);     // MAC de destino
    memset(eth->ether_shost, 0xff, 6);         // MAC origem
    eth->ether_type = htons(ETHERTYPE_CUSTOM); // tipo customizado

    // monta o payload
    int payload_len = 6 + frame->tamanho;
    buffer[TAMANHO_ETH + 0] = frame->marcador_inicio;
    buffer[TAMANHO_ETH + 1] = frame->tamanho;
    buffer[TAMANHO_ETH + 2] = frame->sequencia;
    buffer[TAMANHO_ETH + 3] = frame->tipo;
    buffer[TAMANHO_ETH + 4] = frame->checksum;
    memcpy(&buffer[TAMANHO_ETH + 5], frame->dados, frame->tamanho);

    // envia
    int total = TAMANHO_ETH + 5 + frame->tamanho;
    if (send(socket_fd, buffer, total, 0) == -1)
    {
        perror("Erro ao enviar frame");
        return -1;
    }
    return 0;
}

// recebe um frame
int receber_frame(int socket_fd, Frame *frame, const uchar *filtro_mac)
{
    uchar buffer[1514];
    int n = recv(socket_fd, buffer, sizeof(buffer), 0);
    if (n <= 0)
        return -1;

    struct ether_header *eth = (struct ether_header *)buffer;

    // verifica o tipo
    if (ntohs(eth->ether_type) != ETHERTYPE_CUSTOM)
        return -1;

    // verifica o mac de destino
    if (filtro_mac && memcmp(eth->ether_dhost, filtro_mac, 6) != 0)
        return -1;

    // ponteiro para o inicio do payload
    uchar *dados = buffer + TAMANHO_ETH;

    // verifica o marcador
    if (dados[0] != MARCADOR_INICIO)
        return -1;

    // preenche os campos do frame
    frame->marcador_inicio = dados[0];
    frame->tamanho = dados[1];
    frame->sequencia = dados[2];
    frame->tipo = dados[3];
    frame->checksum = dados[4];
    memcpy(frame->dados, &dados[5], frame->tamanho);

    // retorna 0 se o checksum bater, -2 caso contrario
    return verificar_checksum(frame) ? 0 : -2;
}

// cria o raw socket
int cria_raw_socket(char *nome_interface_rede)
{
    // Cria arquivo para o socket sem qualquer protocolo
    int soquete = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (soquete == -1)
    {
        fprintf(stderr, "Erro ao criar socket: Verifique se você é root!\n");
        exit(-1);
    }

    int ifindex = if_nametoindex(nome_interface_rede);

    struct sockaddr_ll endereco = {0};
    endereco.sll_family = AF_PACKET;
    endereco.sll_protocol = htons(ETH_P_ALL);
    endereco.sll_ifindex = ifindex;
    // Inicializa socket
    if (bind(soquete, (struct sockaddr *)&endereco, sizeof(endereco)) == -1)
    {
        fprintf(stderr, "Erro ao fazer bind no socket\n");
        exit(-1);
    }

    struct packet_mreq mr = {0};
    mr.mr_ifindex = ifindex;
    mr.mr_type = PACKET_MR_PROMISC;
    // Não joga fora o que identifica como lixo: Modo promíscuo
    if (setsockopt(soquete, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) == -1)
    {
        fprintf(stderr, "Erro ao fazer setsockopt: "
                        "Verifique se a interface de rede foi especificada corretamente.\n");
        exit(-1);
    }

    return soquete;
}

// retorna o timestamp atual em ms
long long timestamp_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)(tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

// envia um frame e espera ACK/NACK, retrasmite caso de timeout ou NACK
int enviar_com_ack(int sock, const Frame *frame, const uchar *dest_mac, int timeout_ms)
{
    // maximo de 5 tentativas
    int tentativas = 5;
    Frame resposta;
    uchar seq_esperada = frame->sequencia;

    while (tentativas--)
    {
        // envia o frame
        enviar_frame(sock, frame, dest_mac);
        long long t0 = timestamp_ms();
        // aguarda resposta ate dar timeout
        while ((timestamp_ms() - t0) < timeout_ms)
        {
            if (receber_frame(sock, &resposta, NULL) == 0)
            {
                if (resposta.tipo == 0 && resposta.sequencia == seq_esperada)
                {
                    return 0; // ACK recebido
                }
                else if (resposta.tipo == 1 && resposta.sequencia == seq_esperada)
                {
                    break; // NACK, reenvia
                }
            }
        }
        // se da timeout, reenvia
        if (timestamp_ms() - t0 >= timeout_ms)
        {
            printf("Timeout. Reenviando frame...\n");
        }
    }
    return -1; // falha apos 5 tentativas
}

// recebe um frame e devolve ACK/NACK
int receber_com_ack(int sock, Frame *frame, uchar *mac_origem, int timeout_ms) {
    long long inicio = timestamp_ms();
    while (timestamp_ms() - inicio < timeout_ms) {
        int ret = receber_frame(sock, frame, NULL);
        if (ret == 0) {
            // extrai MAC de origem
            struct ether_header *eth = (struct ether_header *)frame->dados;
            uchar mac[6];
            memcpy(mac, eth->ether_shost, 6);

            // envia ACK de volta (tipo 0)
            Frame ack = criar_frame(frame->sequencia, 0, NULL, 0);
            enviar_frame(sock, &ack, mac);
            if (mac_origem) memcpy(mac_origem, mac, 6);
            return 0;
        }
        else if (ret == -2) {
            // checksum invalido, envia NACK (tipo 1)
            Frame nack = criar_frame(frame->sequencia, 1, NULL, 0);
            struct ether_header *eth = (struct ether_header *)frame->dados;
            uchar mac[6];
            memcpy(mac, eth->ether_shost, 6);
            enviar_frame(sock, &nack, mac);
            // continua aguardando novo frame
        }
    }
    return -1; // timeout sem receber nada valido
}
