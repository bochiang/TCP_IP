#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "xsocket.h"

#include <errno.h>
#include <stdlib.h>
#include <pthread.h>

#ifdef _MSC_VER
#pragma comment(lib, "pthreadVC2.lib")
#include <windows.h>
#define ms_sleep(x)  Sleep(x)
#else
#include <time.h>
#include <sys/timeb.h>
#include <unistd.h>
#define ms_sleep(x)  usleep((x) * 1000)
#define sprintf_s    snprintf
#endif

#define TEST_TCP     1  // 1: TCP通信;   0: UDP组播

#if TEST_TCP
/* 本机IP地址与端口号 */
static const char *s_server_addr = "127.0.0.1";  // argv[3]
static const int   i_server_port = 1012;
#else
/* 组播地址与端口号 */
static const char *s_cast_addr = "233.1.1.101";  // s_cast_addr
static const int   i_cast_port = 1105;
/* 本机IP地址与端口号 */
static const char *s_self_addr = "127.0.0.1";  // argv[3]
static const int   i_self_port = 1012;
#endif

#define BUF_SIZE  4096

// 组播发送：
void* snd(void *arg)
{
    char buf[BUF_SIZE];
    int len;
    int64_t count = 0;

#if TEST_TCP
    socket_t client_sock = INVALID_SOCKET;
    socket_t tcp_sock = socket_create_tcp_listen(s_server_addr, i_server_port);

    printf_s("[server] TCP listen socket: %d\n", tcp_sock);
    printf_s("[server] waiting for connect\n");

    if (tcp_sock == INVALID_SOCKET) {
        return 0;
    }

    for (;;) {
        ms_sleep(100);
        client_sock = socket_create_tcp_server(tcp_sock, 1000);

        if (client_sock != INVALID_SOCKET) {
            printf_s("[server] TCP server: new link: %d\n", client_sock);
            for (;;)
            {
                ms_sleep(200);
#if 1
                int ret = 0;
                len = socket_recv(client_sock, buf, BUF_SIZE);
                if (len == 0) {
                    printf_s("TCP serRecv[%d]: ERROR \n", len);
                }

                if (len > 0) {
                    buf[len] = '\0';
                    printf_s("TCP serRecv[%d]: \"%s\"\n", len, buf);
                }
#endif
                int send = 1;
                sprintf_s(buf, "msg: $d\n", send);
                len = socket_send(client_sock, buf, BUF_SIZE);
                if (len == -1)
                {
                    printf_s("[server] waiting for connect\n");
                    client_sock = socket_create_tcp_server(tcp_sock, 1000);
                }
                if (len > -1)
                {
                    printf_s("TCP send[%d]: \"%s\"\n", len, buf);
                }
                count++;
            }
        }
    }

    if (client_sock != INVALID_SOCKET)
    {
        socket_close(client_sock);
        client_sock = INVALID_SOCKET;
    }

    socket_close(tcp_sock);
#else
    socket_t   mc_sock = socket_create_mc(s_self_addr, s_cast_addr, i_cast_port, 2);
    printf("UDP multi-cast socket: %d\n", mc_sock);

    for (;;)
    {
        ms_sleep(100);

        sprintf_s(buf, BUF_SIZE, "msg: %d, xxxxx", count++);

        len = socket_send(mc_sock, buf, BUF_SIZE);
        printf("UDP sent    [%d]: \"%s\"\n", len, buf);
    }
#endif
    pthread_exit((void *)0);
}

// 组播接收
void* rcv(void*arg)
{
    char buf[BUF_SIZE + 1];
    int len;
#if TEST_TCP
    socket_t tcp_socket = socket_create_tcp_client(s_server_addr, i_server_port);

    printf_s("[client] TCP socket: %d\n", tcp_socket);

    for (;;)
    {
        len = socket_recv(tcp_socket, buf, BUF_SIZE);
        if (len >= 0)
        {
            buf[len] = '\0';
            printf_s("TCP received[%d]: \"%s\"\n", len, buf);
        }
        else
        {
            tcp_socket = socket_create_tcp_client(s_server_addr, i_server_port);
        }
    }
    socket_close(tcp_socket);
#else
    socket_t  udp_client_socket = socket_add_mc(s_self_addr, s_cast_addr, i_cast_port);

    printf("[client] UDP socket: %d\n", udp_client_socket);

    for (;;) {
        len = socket_udp_mc_recv(udp_client_socket, buf, BUF_SIZE);
        if (len >= 0) {
            buf[len] = '\0';
            printf("UDP received[%d]: \"%s\"\n", len, buf);
        }
    }
    socket_close(udp_client_socket);
#endif
    pthread_exit((void *)0);
}


int main()
{
    pthread_t id[2];
    socket_startup();
   // pthread_create(&id[0], NULL, snd, NULL);
    ms_sleep(20);
    pthread_create(&id[1], NULL, rcv, NULL);
    //pthread_join(id[0], NULL);
    pthread_join(id[1], NULL);

    socket_cleanup();
    while (1)
    {
        /*cin >> send_data;
        temp[0] = send_data;
        mySerialPort1.WriteData(temp, 1);*/
    }
    return 0;
}