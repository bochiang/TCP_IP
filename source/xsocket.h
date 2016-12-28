/*----------------------------------------------------------------------------
 *
 *  @file     xsocket.h
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

#ifndef __XSOCKET_H__
#define __XSOCKET_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef int     socket_t;
typedef struct  udpsender udpsender;

// this is used instead of -1, since the socket_t type is unsigned
#ifndef INVALID_SOCKET
#define INVALID_SOCKET  (socket_t)(~0)  // invalid socket
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR            (-1)  // socket error
#endif

// ---------------------------------------------------------------------------
// function declares

/* socket library initial
 */
int32_t socket_startup();

/* socket library destroy
 */
void socket_cleanup();

/* send data with a socket
 */
int32_t socket_send(socket_t fd, char *data, int32_t len);

/* close a socket
 */
void socket_close(socket_t fd);

/* used for a server, obtain a socket to send data onto a multi-cast address
 */
socket_t socket_create_mc(const char *ip_if, const char *ip_grp, const uint16_t port, const char ttl);

/* used for a client, obtain a socket to receive data from a multi-cast address
 */
socket_t socket_add_mc(const char *ip_if, const char *ip_grp, const uint16_t port);

/* used for a server, obtain a socket to accept link with TCP
 */
socket_t socket_create_tcp_listen(const char *s_if_addr, const uint16_t port);

/* used for a server, obtain a socket to send data with TCP
 */
socket_t socket_create_tcp_server(socket_t tcp_listen, int32_t ms_timeout);

/* used for a server, obtain a socket to receive data with TCP
 */
socket_t socket_create_tcp_client(const char *s_server_addr, const uint16_t port);

/* used for a server, can receive data for TCP
 */
int32_t socket_recv(socket_t fd, void *data, int32_t len);

/* used for UDP multi-cast receiving
 */
int32_t socket_udp_mc_recv(socket_t fd, void *data, int len);

/* unused functions */
int32_t socket_recv_from(socket_t fd, void *data, int32_t len);

/* unused functions, UDP sending */
udpsender *socket_create_udp(const char *ip_if, const uint16_t port);
int32_t socket_send_udp(udpsender *sender, void *buffer, int32_t sendlen);
void socket_close_udp(udpsender *sender);
#ifdef __cplusplus
}
#endif

#endif // __XSOCKET_H__
