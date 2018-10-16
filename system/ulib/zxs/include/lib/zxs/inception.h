// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXS_INCEPTION_H_
#define LIB_ZXS_INCEPTION_H_

// This header will be removed once all the socket protocol code is refactored
// from fdio to zxs.

#include <stdint.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define ZXSIO_PAYLOAD_SZ 900

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

#define ZXSIO_HDR_SZ       (__builtin_offsetof(zxsio_msg_t, data))

zx_status_t zxsio_txn(zx_handle_t socket, zxsio_msg_t* msg);

__END_CDECLS

#endif // LIB_ZXS_INCEPTION_H_
