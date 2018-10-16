// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXS_PROTOCOL_H_
#define LIB_ZXS_PROTOCOL_H_

#include <netdb.h>
#include <stdint.h>
#include <sys/socket.h>
#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define ZXSIO_ONE_HANDLE   0x00000100

// SIO (Socket I/O)
#define ZXSIO_CLOSE        0x00000001
#define ZXSIO_OPEN        (0x00000003 | ZXSIO_ONE_HANDLE)
#define ZXSIO_IOCTL        0x0000000a
#define ZXSIO_CONNECT      0x00000010
#define ZXSIO_BIND         0x00000011
#define ZXSIO_LISTEN       0x00000012
#define ZXSIO_GETSOCKNAME  0x00000013
#define ZXSIO_GETPEERNAME  0x00000014
#define ZXSIO_GETSOCKOPT   0x00000015
#define ZXSIO_SETSOCKOPT   0x00000016

#define ZXSIO_SOCKET_DIR_NONE   "none-v3"
#define ZXSIO_SOCKET_DIR_SOCKET "socket-v3"

// fdio signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
#define ZXSIO_SIGNAL_ERROR ZX_USER_SIGNAL_2
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3
#define ZXSIO_SIGNAL_HALFCLOSED ZX_USER_SIGNAL_4

// ZXSIO_GETSOCKNAME
// ZXSIO_GETPEERNAME
typedef struct zxrio_sockaddr_reply {
    struct sockaddr_storage addr;
    socklen_t len;
} zxrio_sockaddr_reply_t;

// ZXSIO_GETSOCKOPT
// ZXSIO_SETSOCKOPT
typedef struct zxrio_sockopt_req_reply {
    int32_t level;
    int32_t optname;
    char optval[128];
    socklen_t optlen;
} zxrio_sockopt_req_reply_t;

// wire format for datagram messages
typedef struct fdio_socket_msg {
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int32_t flags;
    char data[1]; // variable size
} fdio_socket_msg_t;

#define FDIO_SOCKET_MSG_HEADER_SIZE offsetof(fdio_socket_msg_t, data)

__END_CDECLS

#endif // LIB_ZXS_PROTOCOL_H_
