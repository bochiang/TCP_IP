/*----------------------------------------------------------------------------
 *
 *  @file     xsocket.c
 *  @brief    A simple socket with support for Windows/Linux
 *
 *  UDP/TCP socket operation wrapper
 *
 *  @author   Falei LUO
 *  @email    falei.luo@gmail.com
 *  @version  1.0.0.2
 *  @date     2016-04-21
 *  @license  Not Public
 *
 *----------------------------------------------------------------------------*
 *  Remark         : Description                                              *
 *----------------------------------------------------------------------------*
 *  Change History :                                                          *
 *  <Date>     | <Version> | <Author>  | <Description>                        *
 *----------------------------------------------------------------------------*
 *  2016-04-21 | 1.0.0.1   | Falei LUO | Create file                          *
 *  2016-04-25 | 1.0.0.2   | Falei LUO | Fix interface name and add TCP       *
 *  2016-04-26 | 1.0.0.2   | Falei LUO | Fix UDP multi-cast client buffer     *
 *  2016-05-10 | 1.0.0.3   | Falei LUO | Fix UDP multi-cast recv err on Linux *
 *  2016-05-11 | 1.0.0.4   | Falei LUO | Simplify the header, not include     *
 *                                       system depended headers              *
 *  2016-05-28 | 1.0.0.5   | Falei LUO | Fix Compiling warnings               *
 *----------------------------------------------------------------------------*
 *----------------------------------------------------------------------------*/

#ifdef __GNUC__
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#endif
// ---------------------------------------------------------------------------
// for socket in windows
#ifdef _MSC_VER
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#pragma warning(disable: 4127)
#include <ws2tcpip.h>
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32")        // Ws2_32.lib
#define error_no()  WSAGetLastError() // get the error no
#endif
#include <string.h>
#include "xsocket.h"


struct udpsender {
    socket_t fd;
    struct sockaddr_in server;
    int32_t len;
};

#define MAX_CONN      5               // queue length specifiable by listen
#define LOCAL_HOST    "127.0.0.1"     // local host

/* 用于控制系统接收/发送数据的缓冲区的阈值 */
static const int size_flush_buf_min = 16  << 20; // 16 MB
static const int size_flush_buf_max = 128 << 20; // 128 MB

// ---------------------------------------------------------------------------
// Function   : set socket as blocking or non-blocking socket
// Parameters :
//      [in ] : fd - the socket
//            : on - on/off (non-blocking / blocking)
//      [out] : none
// Return     : zero on success, otherwise failed
// ---------------------------------------------------------------------------
static int32_t
set_non_blocking(socket_t fd, int32_t on)
{
#ifdef WIN32
    if (ioctlsocket(fd, FIONBIO, (unsigned long *)&on) == SOCKET_ERROR) {
        return -1;
    }

    return 0;

#else
    int32_t flags;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
        return -1;
    }

    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        return -1;
    }
    return 0;
# endif
}

// ---------------------------------------------------------------------------
// Function   : close a socket
// Parameters :
//      [in ] : fd - the socket
//      [out] : none
// Return     : none
// ---------------------------------------------------------------------------
void
socket_close(socket_t fd)
{
#ifdef WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

// ---------------------------------------------------------------------------
// Function   : initiate use of the WinSock DLL by a process
// Parameters :
//      [in ] : none
//      [out] : none
// Return     : zero on success, otherwise failed
// ---------------------------------------------------------------------------
int32_t
socket_startup(void)
{
#ifdef WIN32
    WSADATA wsaData;

    // is WinSock DLL acceptable?
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        return -1;                  // can not find a usable WinSock DLL
    }

    // confirm that the WinSock DLL supports 2.2
    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        WSACleanup();
        return -1;                  // can not find a usable WinSock DLL
    }
#endif

    return 0;
}

// ---------------------------------------------------------------------------
// Function   :  terminates use of the WinSock 2 DLL (Ws2_32.dll)
// Parameters :
//      [in ] : none
//      [out] : none
// Return     : none
// ---------------------------------------------------------------------------
void
socket_cleanup(void)
{
#ifdef WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Function   : create a listening socket (used by SERVER)
// Parameters :
//      [in ] : s_if_ip - IP of interface
//      [in ] : port    - the port we want to listen to
//      [out] : none
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// ---------------------------------------------------------------------------
socket_t
socket_create_tcp_listen(const char *s_if_ip, const uint16_t port)
{
    socket_t fd;
    struct sockaddr_in sa;

    // creates a STREAM socket
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    // make a local socket address
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = inet_addr(s_if_ip);
    sa.sin_port        = htons(port);

    // associates a local address with the socket
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        socket_close(fd);
        return INVALID_SOCKET;
    }

    // let the socket listen for an incoming connection
    if (listen(fd, MAX_CONN) != 0) {
        socket_close(fd);
        return INVALID_SOCKET;
    }

    // mark as non-blocking
    if (set_non_blocking(fd, 1) != 0) {
        socket_close(fd);
        return INVALID_SOCKET;
    }

    return fd;
}


/* ---------------------------------------------------------------------------
 * Function   : create a TCP client socket (used by client)
 * Parameters :
 *      [in ] : s_server_addr - the IP address of a server
 *      [in ] : server_port   - the port we want to link to
 *      [out] : socket
 * Return     : a descriptor referencing the socket or INVALID_SOCKET on error
 * ---------------------------------------------------------------------------
 */
socket_t 
socket_create_tcp_server(socket_t tcp_listen, int32_t ms_timeout)
{
    fd_set    fds;
    int       ret;
    struct timeval timeout;

    FD_ZERO(&fds);
    FD_SET(tcp_listen, &fds);

    timeout.tv_sec  = ms_timeout / 1000;
    timeout.tv_usec = (ms_timeout % 1000) * 1000;

    ret = select(tcp_listen + 1, &fds, NULL, NULL, &timeout);

    switch (ret) {
    case -1:
        break;                    // error
    case 0:
        break;
    default:
        if (FD_ISSET(tcp_listen, &fds)) {
            /* accept a link */
            socket_t s = accept(tcp_listen, NULL, NULL);
            if (s != INVALID_SOCKET && s != SOCKET_ERROR) {
                return s;
            }
        }
        break;
    }

    return INVALID_SOCKET;
}


/* ---------------------------------------------------------------------------
 * Function   : create a TCP client socket (used by client)
 * Parameters :
 *      [in ] : s_server_addr - the IP address of a server
 *      [in ] : server_port   - the port we want to link to
 *      [out] : socket
 * Return     : a descriptor referencing the socket or INVALID_SOCKET on error
 * ---------------------------------------------------------------------------
 */
socket_t
socket_create_tcp_client(const char *s_server_addr, const uint16_t server_port)
{
    socket_t sc_client; // 连接套接字
    struct sockaddr_in sa_server; // 服务器地址信息
    int ret;

    // 创建Socket,使用TCP协议
    sc_client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sc_client == INVALID_SOCKET) {
        printf("socket() failed!\n");
        return INVALID_SOCKET;
    }

    // 构建服务器地址信息  
    sa_server.sin_family = AF_INET; //地址家族  
    sa_server.sin_port = htons(server_port); //注意转化为网络节序  
    sa_server.sin_addr.s_addr = inet_addr(s_server_addr);

    // 连接服务器  
    ret = connect(sc_client, (struct sockaddr *)&sa_server, sizeof(sa_server));
    if (ret == SOCKET_ERROR) {
        printf("connect() failed!\n");
        socket_close(sc_client); // 关闭套接字
        return INVALID_SOCKET;
    }

    return sc_client;
}

// ***************************************************************************
// udp send
// ***************************************************************************
udpsender *
socket_create_udp(const char *ip_if, const uint16_t port)
{
    udpsender *sender = (udpsender *)malloc(sizeof(udpsender));
    // creates a STREAM socket
    if ((sender->fd = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        sender->fd = 0;
        return NULL;
    }

    sender->len = sizeof(sender->server);
    sender->server.sin_family = AF_INET;
    sender->server.sin_port = htons(port);
    sender->server.sin_addr.s_addr = inet_addr(ip_if);

    return sender;
}

int32_t
socket_send_udp(udpsender *sender, void *buffer, int32_t sendlen)
{
    int len = sendto(sender->fd, buffer, sendlen, 0, (struct sockaddr *)&sender->server, sender->len);
    return len;
}

void
socket_close_udp(udpsender *sender)
{
    socket_close(sender->fd);
    sender->fd = 0;
    free(sender);
}


// ***************************************************************************
// * multicast
// ***************************************************************************

// ---------------------------------------------------------------------------
// Function   : create a multicast socket (used by data sending SERVER)
// Parameters :
//      [in ] : ip_if  - ip address of interface
//            : ip_grp - ip address of multicast
//            : port   - the port we want to listen to
//            : ttl    - the port we want to listen to
//      [out] : none
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// Marks      : multicast addresses is from 224.0.0.0 to 239.255.255.255
// ---------------------------------------------------------------------------
socket_t
socket_create_mc(const char *ip_if, const char *ip_grp, const uint16_t port, const char ttl)
{
    struct sockaddr_in  local_addr;
    struct sockaddr_in  group_addr;
    struct in_addr      if_addr;
    socket_t fd;
    char opt;
    if (!IN_MULTICAST(ntohl(inet_addr(ip_grp)))) {
        printf("invalid multi-cast address: %s\n", ip_grp);
        return INVALID_SOCKET;            // invalid multicast group address
    }
    // create a DATAGRAMS socket
    if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        printf("create datagrams socket error: %s\n", ip_grp);
        return INVALID_SOCKET;            // create socket error
    }

#if 0
    // allow local address reuse
    opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        socket_close(fd);
        return INVALID_SOCKET;            // set socket option error
    }
#endif

    // disable loop back
    opt = 0;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &opt, sizeof(opt))) {
        printf("disable loop back error: %s\n", ip_grp);
        socket_close(fd);
        return INVALID_SOCKET;            // set socket option error
    }

    // set time-to-live, controls scope of a multicast session
    opt = ttl;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &opt, sizeof(opt))) {
        printf("set time-to-live error: %s\n", ip_grp);
        socket_close(fd);
        return INVALID_SOCKET;            // set socket option error
    }

    // set interface to be used for multicast
    memset(&if_addr, 0, sizeof(if_addr));
    if_addr.s_addr = inet_addr(ip_if);
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&if_addr, sizeof(if_addr))) {
        printf("set interface error: %s\n", ip_if);
        socket_close(fd);
        return INVALID_SOCKET;            // set socket option error
    }
    // mark as non-blocking
    if (set_non_blocking(fd, 1) != 0) {
        printf("Error marking as non-blocking: %s\n", ip_grp);
        socket_close(fd);
        return INVALID_SOCKET;
    }
    // make a local socket address
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port        = htons(port);

    // associates a local address with the socket
    if (bind(fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        printf("Error associcates a local address: %d, %d\n", port, INADDR_ANY);
        socket_close(fd);
        return INVALID_SOCKET;            // bind error
    }
    // connect to the destination multicast address
    memset(&group_addr, 0, sizeof(group_addr));
    group_addr.sin_family      = AF_INET;
    group_addr.sin_addr.s_addr = inet_addr(ip_grp);
    group_addr.sin_port        = htons(port);
    if (connect(fd, (struct sockaddr *)&group_addr, sizeof(group_addr)) != 0) {
        printf("Error connecting multicast address: %s\n", ip_grp);
        socket_close(fd);
        return INVALID_SOCKET;            // connect error
    }
    return fd;
}

#if !defined(_MSC_VER)
/* UDP multi-cast socket for recving */
static struct sockaddr_in udp_mc_servaddr;
#endif


// ---------------------------------------------------------------------------
// Function   : create a multicast socket and add to a group (used by data receiving)
// Parameters :
//      [in ] : ip_if  - ip address of interface
//            : ip_grp - ip address of multicast
//            : port   - the multicast port we want to connect to
//      [out] : none
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// Marks      : multicast addresses is from 224.0.0.0 to 239.255.255.255
// ---------------------------------------------------------------------------
socket_t
socket_add_mc(const char *ip_if, const char *ip_grp, const uint16_t port)
{
    socket_t sockfd;
    int rcvbuf_len = 0;
    int len = sizeof(rcvbuf_len);
    struct ip_mreq      mreq;
#if defined(_MSC_VER)
    struct sockaddr_in  local_addr;
    // struct sockaddr_in  group_addr;
    // struct in_addr      if_addr;

    if (!IN_MULTICAST(ntohl(inet_addr(ip_grp)))) {
        printf("invalid multi-cast address: %s\n", ip_grp);
        return INVALID_SOCKET;            // invalid multicast group address
    }

    // create a DATAGRAMS socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
        printf("Error creating datagrams socket: %s\n", ip_grp);
        return INVALID_SOCKET;            // create socket error
    }

    // make a local socket address
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family      = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(ip_if); // htonl(INADDR_ANY);
    local_addr.sin_port        = htons(port);

    // associates a local address with the socket
    if (bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
        printf("Error associates a local address: %s port %d\n", ip_if, port);
        socket_close(sockfd);
        return INVALID_SOCKET;            // bind error
    }

    memset(& mreq, 0, sizeof(mreq)) ;
    mreq.imr_multiaddr.s_addr   = inet_addr(ip_grp);    //  mcast IP (port specified when we bind)
    mreq.imr_interface.s_addr   = inet_addr(ip_if);     //  over this NIC

    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq))) {
        printf("Error set option over ip: %s port %d\n", ip_if, port);
        socket_close(sockfd);
        return INVALID_SOCKET;            // set socket option error
    }
#else
    struct in_addr ia;
    struct hostent *group;

    printf("Warning: ip_if %s not supported to set on Linux, Please use \"route add -net 224.0.0.0 netmask 224.0.0.0 eth0\"(or similar)\n",
           ip_if);

    /*创建socket用于UDP通讯*/
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        printf("socket creating err in udptalk\n");
        exit(1);
    }

    /*设置要加入组播的地址*/
    bzero(&mreq, sizeof(struct ip_mreq));
    if ((group = gethostbyname(ip_grp)) == (struct hostent*)0) {
        perror("gethostbyname");
        exit(errno);
    }

    bcopy((void*)group->h_addr, (void*)&ia, group->h_length);
    /*设置组地址*/
    bcopy(&ia, &mreq.imr_multiaddr.s_addr, sizeof(struct in_addr));

    /*设置发送组播消息的源主机的地址信息*/
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    /*把本机加入组播地址， 即本机卡作为组播成员， 只要加入组才能收到组播消息*/
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(struct ip_mreq)) == -1) {
        perror("setsockopt");
        exit(-1);
    }

    memset(&udp_mc_servaddr, 0, sizeof(struct sockaddr_in));
    udp_mc_servaddr.sin_family = AF_INET;

    /*组播端口号*/
    udp_mc_servaddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip_grp, &udp_mc_servaddr.sin_addr) < 0) {
        printf("Wrong dest IP address!\n");
        exit(0);
    }

    /*绑定自己的端口和IP信息到socket上*/
    if (bind(sockfd, (struct sockaddr*)&udp_mc_servaddr, sizeof(struct sockaddr_in)) == -1) {
        printf("Bind error\n");
        exit(0);
    }
#endif

    if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)&rcvbuf_len, &len) < 0) {
        perror("getsockopt: ");
        socket_close(sockfd);
        return INVALID_SOCKET;
    }

    printf("[xsocket] the receive buf old len: %d KB\n", ((rcvbuf_len + 512) >> 10));

    rcvbuf_len *= 1024;
    if (rcvbuf_len < size_flush_buf_min) {
        rcvbuf_len = size_flush_buf_min;
    } else if (rcvbuf_len > size_flush_buf_max) {
        rcvbuf_len = size_flush_buf_max;
    }

    len = sizeof(rcvbuf_len);
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)&rcvbuf_len, len) < 0) {
        perror("setsockopt: ");
        socket_close(sockfd);
        return INVALID_SOCKET;
    }

    printf("[xsocket] the receive buf new len: %d KB\n", ((rcvbuf_len + 512) >> 10));
    // mark as non-blocking
    if (set_non_blocking(sockfd, 1) != 0) {
        socket_close(sockfd);
        return INVALID_SOCKET;
    }
    return sockfd;
}


// ---------------------------------------------------------------------------
// Function   : send data through socket
// Parameters :
//      [in ] : fd   - a descriptor identifying a connected socket
//            : data - a pointer to a buffer containing the data to be transmitted
//            : len  - the length of the data in buffer
//      [out] : none
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// Marks      : multicast addresses is from 224.0.0.0 to 239.255.255.255
// ---------------------------------------------------------------------------
int32_t
socket_send(socket_t fd, char *data, int32_t len)
{
    return send(fd, (const char *)data, len, 0); //MSG_DONTROUTE);
}



// ---------------------------------------------------------------------------
// Function   : send data through socket
// Parameters :
//      [in ] : fd   - a descriptor identifying a connected socket
//      [out] : data - a pointer to a buffer containing the data to be transmitted
//            : len  - the length of the data in buffer
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// Marks      : multicast addresses is from 224.0.0.0 to 239.255.255.255
// ---------------------------------------------------------------------------
int32_t
socket_recv(socket_t fd, void *data, int32_t len)
{
    return recv(fd, data, len, 0); //MSG_DONTROUTE);
}


// ---------------------------------------------------------------------------
// Function   : recv data through a UDP multi-cast receiving socket
// Parameters :
//      [in ] : fd   - a descriptor identifying a connected socket
//            : data - a pointer to a buffer containing the data to be transmitted
//            : len  - the maximum length of the buffer
// Return     : received data length or INVALID_SOCKET on error
// ---------------------------------------------------------------------------
int32_t socket_udp_mc_recv(socket_t fd, void *data, int len)
{
#if defined(_MSC_VER)
    return socket_recv(fd, data, len);
#else
    unsigned int socklen = sizeof(udp_mc_servaddr);
    return recvfrom(fd, data, len, 0, (struct sockaddr *)&udp_mc_servaddr, &socklen);
#endif
}


// ---------------------------------------------------------------------------
// Function   : recv udp data through socket
// Parameters :
//      [in ] : fd   - a descriptor identifying a connected socket
//      [out] : data - a pointer to a buffer containing the data to be transmitted
//            : len  - the length of the data in buffer
// Return     : a descriptor referencing the socket or INVALID_SOCKET on error
// Marks      : multicast addresses is from 224.0.0.0 to 239.255.255.255
// ---------------------------------------------------------------------------
int32_t
socket_recv_from(socket_t fd, void *data, int32_t len)
{
    struct sockaddr_in in_addr;
    int in_addr_len = sizeof(in_addr);
    return recvfrom(fd, data, len, 0, (struct sockaddr *)&in_addr, &in_addr_len);
}

