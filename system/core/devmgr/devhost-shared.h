// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/types.h>

#define DC_MAX_DATA 4096

namespace devmgr {

// The first two fields of devcoordinator messages align
// with those of remoteio messages so we avoid needing a
// dedicated channel for forwarding OPEN operations.
// Our opcodes set the high bit to avoid overlap.
struct Message {
    zx_txid_t txid;     // FIDL message header
    uint32_t reserved0;

    uint32_t flags;

    enum struct Op : uint32_t {
        // This bit differentiates DC OPs from RIO OPs
        kIdBit = 0x10000000,

        // Coord->Host Ops
        kCreateDeviceStub = 0x10000001,
        kCreateDevice = 0x10000002,
        kBindDriver = 0x10000003,
        kConnectProxy = 0x10000004,
        kSuspend = 0x10000005,

        // Host->Coord Ops
        kStatus = 0x10000010,
        kAddDevice = 0x10000011,
        kAddDeviceInvisible = 0x10000012,
        kRemoveDevice = 0x10000013,  // also Coord->Host
        kMakeVisible = 0x10000014,
        kBindDevice = 0x10000015,
        kGetTopoPath = 0x10000016,
        kLoadFirmware = 0x10000017,
        kGetMetadata = 0x10000018,
        kAddMetadata = 0x10000019,
        kPublishMetadata = 0x1000001a,

        // Host->Coord Ops for DmCtl
        kDmCommand = 0x10000020,
        kDmOpenVirtcon = 0x10000021,
        kDmWatch = 0x10000022,
        kDmMexec = 0x10000023,
    } op;

    union {
        zx_status_t status;
        uint32_t protocol_id;
        uint32_t value;
    };
    uint32_t datalen;
    uint32_t namelen;
    uint32_t argslen;

    uint8_t data[DC_MAX_DATA];
};

struct Status {
    zx_txid_t txid;
    zx_status_t status;
};

#define DC_PATH_MAX 1024

zx_status_t dc_msg_pack(Message* msg, uint32_t* len_out,
                        const void* data, size_t datalen,
                        const char* name, const char* args);
zx_status_t dc_msg_unpack(Message* msg, size_t len, const void** data,
                          const char** name, const char** args);
zx_status_t dc_msg_rpc(zx_handle_t h, Message* msg, size_t msglen,
                       zx_handle_t* handles, size_t hcount,
                       Status* rsp, size_t rsp_len, size_t* resp_actual,
                       zx_handle_t* outhandle);

} // namespace devmgr
