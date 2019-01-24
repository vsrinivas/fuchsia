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

// wire format for datagram messages
typedef struct fdio_socket_msg {
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int32_t flags;
    char data[1]; // variable size
} fdio_socket_msg_t;

#define FDIO_SOCKET_MSG_HEADER_SIZE offsetof(fdio_socket_msg_t, data)

// TODO(NET-1865): remove this after the next Chromium roll.
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

__END_CDECLS

#endif // LIB_ZXS_PROTOCOL_H_
