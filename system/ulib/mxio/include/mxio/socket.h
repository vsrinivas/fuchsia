// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/device/ioctl.h>

#include <mxio/io.h>

#include <stdint.h>

#include <sys/socket.h>
#include <netdb.h>

__BEGIN_CDECLS

#define MXRIO_SOCKET_DIR_NONE   "none"
#define MXRIO_SOCKET_DIR_SOCKET "socket"
#define MXRIO_SOCKET_DIR_ACCEPT "accept"

// mxio signals
#define MXSIO_SIGNAL_INCOMING MX_USER_SIGNAL_0
#define MXSIO_SIGNAL_OUTGOING MX_USER_SIGNAL_1
#define MXSIO_SIGNAL_ERROR MX_USER_SIGNAL_2
#define MXSIO_SIGNAL_CONNECTED MX_USER_SIGNAL_3
#define MXSIO_SIGNAL_HALFCLOSED MX_USER_SIGNAL_4

// MXRIO_GETADDRINFO
#define MXRIO_GAI_REQ_NODE_MAXLEN 256
#define MXRIO_GAI_REQ_SERVICE_MAXLEN 256

typedef struct mxrio_gai_req {
    uint8_t node_is_null;
    uint8_t service_is_null;
    uint8_t hints_is_null;
    uint8_t reserved;
    uint32_t reserved2;
    char node[MXRIO_GAI_REQ_NODE_MAXLEN];
    char service[MXRIO_GAI_REQ_SERVICE_MAXLEN];
    struct addrinfo hints;
} mxrio_gai_req_t;

#define MXRIO_GAI_REPLY_MAX 4

typedef struct mxrio_gai_reply {
    // 'res[0].ai' should be the first field
    struct {
        struct addrinfo ai;
        struct sockaddr_storage addr;
    } res[MXRIO_GAI_REPLY_MAX];
    int32_t nres;
    int32_t retval;
} mxrio_gai_reply_t;

typedef union {
    mxrio_gai_req_t req;
    mxrio_gai_reply_t reply;
} mxrio_gai_req_reply_t;

// MXRIO_GETSOCKNAME
// MXRIO_GETPEERNAME
typedef struct mxrio_sockaddr_reply {
    struct sockaddr_storage addr;
    socklen_t len;
} mxrio_sockaddr_reply_t;

// MXRIO_GETSOCKOPT
// MXRIO_SETSOCKOPT
typedef struct mxrio_sockopt_req_reply {
    int32_t level;
    int32_t optname;
    char optval[8];
    socklen_t optlen;
} mxrio_sockopt_req_reply_t;

// wire format for datagram messages
typedef struct mxio_socket_msg {
    struct sockaddr_storage addr;
    socklen_t addrlen;
    int32_t flags;
    char data[1]; // variable size
} mxio_socket_msg_t;

#define MXIO_SOCKET_MSG_HEADER_SIZE offsetof(mxio_socket_msg_t, data)

__END_CDECLS
