// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "private.h"

#define ZXSIO_PAYLOAD_SZ 900
#define ZXSIO_HDR_SZ       (__builtin_offsetof(zxsio_msg_t, data))

// Flags for zxsio.flags

// Set if listen() was called for this socket.
#define ZXSIO_DID_LISTEN (1<<0)

typedef struct zxsio zxsio_t;

struct zxsio {
    // base fdio io object
    fdio_t io;

    // socket handle
    zx_handle_t s;

    // see ZXSIO flags above
    uint32_t flags;
};

typedef struct zxsio_msg zxsio_msg_t;

// TODO: most of these fields will end up unused. Figure out which are needed.
// For now, we keep them to preserve the message header format of zxrio_msg
// to make the conversion process easier.
struct zxsio_msg {
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
};
