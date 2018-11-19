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

// SIO (Socket I/O)
#define ZXSIO_CLOSE        0x00000001
#define ZXSIO_IOCTL        0x0000000a
#define ZXSIO_CONNECT      0x00000010
#define ZXSIO_BIND         0x00000011
#define ZXSIO_LISTEN       0x00000012
#define ZXSIO_GETSOCKNAME  0x00000013
#define ZXSIO_GETPEERNAME  0x00000014
#define ZXSIO_GETSOCKOPT   0x00000015
#define ZXSIO_SETSOCKOPT   0x00000016

// SIO signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
#define ZXSIO_SIGNAL_ERROR ZX_USER_SIGNAL_2
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3

// wire format for datagram messages
typedef struct fdio_socket_msg {
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int32_t flags;
    char data[1]; // variable size
} fdio_socket_msg_t;

#define FDIO_SOCKET_MSG_HEADER_SIZE offsetof(fdio_socket_msg_t, data)

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

#define ZXSIO_PAYLOAD_SZ 900

// TODO: most of these fields will end up unused. Figure out which are needed.
// For now, we keep them to preserve the message header format of zxrio_msg
// to make the conversion process easier.
typedef struct zxsio_msg {
    zx_txid_t txid;                    // FIDL message header
    uint32_t reserved0;
    uint32_t flags;
    uint32_t op;

    uint32_t datalen;                  // size of data[]
    int32_t arg;                       // tx: argument, rx: return value
    union {
        int64_t off;                   // tx/rx: offset where needed
        uint32_t mode;                 // tx: Open
        uint32_t protocol;             // rx: Open
        uint32_t op;                   // tx: Ioctl
    } arg2;
    int32_t reserved1;
    uint32_t hcount;                   // number of valid handles
    zx_handle_t handle[4];             // up to 3 handles + reply channel handle
    uint8_t data[ZXSIO_PAYLOAD_SZ];    // payload
} zxsio_msg_t;

#define ZXSIO_HDR_SZ       (__builtin_offsetof(zxsio_msg_t, data))

__END_CDECLS

#endif // LIB_ZXS_PROTOCOL_H_
