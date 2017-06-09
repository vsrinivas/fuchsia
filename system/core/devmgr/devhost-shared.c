// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Code shared between devhost and devmgr

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/port.h>

#include "devcoordinator.h"

mx_status_t dc_msg_pack(dc_msg_t* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args) {
    uint32_t max = DC_MAX_DATA;
    uint8_t* ptr = msg->data;

    if (data) {
        if (datalen > max) {
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, data, datalen);
        max -= datalen;
        ptr += datalen;
        msg->datalen = datalen;
    } else {
        msg->datalen = 0;
    }
    if (name) {
        datalen = strlen(name) + 1;
        if (datalen > max) {
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, name, datalen);
        max -= datalen;
        ptr += datalen;
        msg->namelen = datalen;
    } else {
        msg->namelen = 0;
    }
    if (args) {
        datalen = strlen(args) + 1;
        if (datalen > max) {
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(ptr, args, datalen);
        ptr += datalen;
        msg->argslen = datalen;
    } else {
        msg->argslen = 0;
    }
    *len_out = sizeof(dc_msg_t) - DC_MAX_DATA + (ptr - msg->data);
    return MX_OK;
}


mx_status_t dc_msg_unpack(dc_msg_t* msg, size_t len, const void** data,
                          const char** name, const char** args) {
    if (len < (sizeof(dc_msg_t) - DC_MAX_DATA)) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }
    len -= sizeof(dc_msg_t);
    uint8_t* ptr = msg->data;
    if (msg->datalen) {
        if (msg->datalen > len) {
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        *data = ptr;
        ptr += msg->datalen;
        len -= msg->datalen;
    } else {
        *data = NULL;
    }
    if (msg->namelen) {
        if (msg->namelen > len) {
            return MX_ERR_BUFFER_TOO_SMALL;
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
            return MX_ERR_BUFFER_TOO_SMALL;
        }
        *args = (char*) ptr;
        ptr[msg->argslen - 1] = 0;
    } else {
        *args = "";
    }
    return MX_OK;
}

mx_status_t dc_msg_rpc(mx_handle_t h, dc_msg_t* msg, size_t msglen,
                       mx_handle_t* handles, size_t hcount,
                       dc_status_t* rsp, size_t rsplen) {
    mx_channel_call_args_t args = {
        .wr_bytes = msg,
        .wr_handles = handles,
        .rd_bytes = rsp,
        .rd_handles = NULL,
        .wr_num_bytes = msglen,
        .wr_num_handles = hcount,
        .rd_num_bytes = rsplen,
        .rd_num_handles = 0,
    };

    //TODO: incrementing txids
    msg->txid = 1;
    mx_status_t r;
    if ((r = mx_channel_call(h, 0, MX_TIME_INFINITE,
                             &args, &args.rd_num_bytes, &args.rd_num_handles,
                             NULL)) < 0) {
        for (size_t n = 0; n < hcount; n++) {
            mx_handle_close(handles[n]);
        }
        return r;
    }
    if (args.rd_num_bytes < sizeof(dc_status_t)) {
        return MX_ERR_INTERNAL;
    }

    return rsp->status;
}
