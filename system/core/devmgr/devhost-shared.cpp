// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code shared between devhost and devmgr

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "devhost-shared.h"

namespace devmgr {

zx_status_t dc_msg_pack(Message* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args) {
    size_t max = DC_MAX_DATA;
    uint8_t* ptr = msg->data;

    if (data) {
        if (datalen > max) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, data, datalen);
        max -= datalen;
        ptr += datalen;
        msg->datalen = static_cast<uint32_t>(datalen);
    } else {
        msg->datalen = 0;
    }
    if (name) {
        datalen = strlen(name) + 1;
        if (datalen > max) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, name, datalen);
        max -= datalen;
        ptr += datalen;
        msg->namelen = static_cast<uint32_t>(datalen);
    } else {
        msg->namelen = 0;
    }
    if (args) {
        datalen = strlen(args) + 1;
        if (datalen > max) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, args, datalen);
        ptr += datalen;
        msg->argslen = static_cast<uint32_t>(datalen);
    } else {
        msg->argslen = 0;
    }
    *len_out = static_cast<uint32_t>(sizeof(Message) - DC_MAX_DATA + (ptr - msg->data));
    return ZX_OK;
}


zx_status_t dc_msg_unpack(Message* msg, size_t len, const void** data,
                          const char** name, const char** args) {
    if (len < (sizeof(Message) - DC_MAX_DATA)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    len -= sizeof(Message);
    uint8_t* ptr = msg->data;
    if (msg->datalen) {
        if (msg->datalen > len) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *data = ptr;
        ptr += msg->datalen;
        len -= msg->datalen;
    } else {
        *data = nullptr;
    }
    if (msg->namelen) {
        if (msg->namelen > len) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *name = (char*) ptr;
        ptr[msg->namelen - 1] = 0;
        ptr += msg->namelen;
        len -= msg->namelen;
    } else {
        *name = "";
    }
    if (msg->argslen) {
        if (msg->argslen > len) {
            return ZX_ERR_BUFFER_TOO_SMALL;
        }
        *args = (char*) ptr;
        ptr[msg->argslen - 1] = 0;
    } else {
        *args = "";
    }
    return ZX_OK;
}

zx_status_t dc_msg_rpc(zx_handle_t h, Message* msg, size_t msglen,
                       zx_handle_t* handles, size_t hcount,
                       Status* rsp, size_t rsplen, size_t* resp_actual,
                       zx_handle_t* outhandle) {
    zx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = handles,
        .rd_bytes = rsp,
        .rd_handles = outhandle,
        .wr_num_bytes = static_cast<uint32_t>(msglen),
        .wr_num_handles = static_cast<uint32_t>(hcount),
        .rd_num_bytes = static_cast<uint32_t>(rsplen),
        .rd_num_handles = outhandle ? 1u : 0u,
    };

    if (outhandle) {
        *outhandle = ZX_HANDLE_INVALID;
    }

    //TODO: incrementing txids
    msg->txid = 1;
    zx_status_t r;
    if ((r = zx_channel_call(h, 0, ZX_TIME_INFINITE,
                             &args, &args.rd_num_bytes, &args.rd_num_handles)) < 0) {
        return r;
    }
    if (args.rd_num_bytes < sizeof(Status)) {
        return ZX_ERR_INTERNAL;
    }
    if (resp_actual) {
        *resp_actual = args.rd_num_bytes;
    }

    return rsp->status;
}

} // namespace devmgr
