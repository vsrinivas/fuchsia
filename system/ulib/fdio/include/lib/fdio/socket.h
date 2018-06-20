// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/device/ioctl.h>

#include <lib/fdio/io.h>

#include <stdint.h>

#include <sys/socket.h>
#include <netdb.h>

__BEGIN_CDECLS

#define ZXRIO_SOCKET_DIR_NONE   "none-v3"
#define ZXRIO_SOCKET_DIR_SOCKET "socket-v3"

// fdio signals
#define ZXSIO_SIGNAL_INCOMING ZX_USER_SIGNAL_0
#define ZXSIO_SIGNAL_OUTGOING ZX_USER_SIGNAL_1
#define ZXSIO_SIGNAL_ERROR ZX_USER_SIGNAL_2
#define ZXSIO_SIGNAL_CONNECTED ZX_USER_SIGNAL_3
#define ZXSIO_SIGNAL_HALFCLOSED ZX_USER_SIGNAL_4

// ZXRIO_GETADDRINFO
#define ZXRIO_GAI_REQ_NODE_MAXLEN 256
#define ZXRIO_GAI_REQ_SERVICE_MAXLEN 256

typedef struct zxrio_gai_req {
    uint8_t node_is_null;
    uint8_t service_is_null;
    uint8_t hints_is_null;
    uint8_t reserved;
    uint32_t reserved2;
    char node[ZXRIO_GAI_REQ_NODE_MAXLEN];
    char service[ZXRIO_GAI_REQ_SERVICE_MAXLEN];
    struct addrinfo hints;
} zxrio_gai_req_t;

#define ZXRIO_GAI_REPLY_MAX 4

typedef struct zxrio_gai_reply {
    // 'res[0].ai' should be the first field
    struct {
        struct addrinfo ai;
        struct sockaddr_storage addr;
    } res[ZXRIO_GAI_REPLY_MAX];
    int32_t nres;
    int32_t retval;
} zxrio_gai_reply_t;

typedef union {
    zxrio_gai_req_t req;
    zxrio_gai_reply_t reply;
} zxrio_gai_req_reply_t;

// ZXRIO_GETSOCKNAME
// ZXRIO_GETPEERNAME
typedef struct zxrio_sockaddr_reply {
    struct sockaddr_storage addr;
    socklen_t len;
} zxrio_sockaddr_reply_t;

// ZXRIO_GETSOCKOPT
// ZXRIO_SETSOCKOPT
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
