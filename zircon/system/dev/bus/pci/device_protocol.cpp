// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "common.h"
#include "device.h"

#include <lib/zx/channel.h>
#include <string.h>
// TODO(ZX-3927): Stop depending on the types in this file.
#include <zircon/syscalls/pci.h>

#define RPC_ENTRY \
    pci_tracef("[%s] %s: entry\n", cfg_->addr(), __func__)

#define RPC_UNIMPLEMENTED \
    RPC_ENTRY;            \
    return RpcReply(ch, ZX_ERR_NOT_SUPPORTED)

namespace pci {

zx_status_t Device::DdkRxrpc(zx_handle_t channel) {
    if (channel == ZX_HANDLE_INVALID) {
        // A new connection has been made, there's nothing else to do.
        return ZX_OK;
    }

    // Clear the buffers. We only servce new requests after we've finished
    // previous messages, so we won't overwrite data here.
    memset(&request_, 0, sizeof(request_));
    memset(&response_, 0, sizeof(response_));

    uint32_t bytes_in;
    uint32_t handles_in;
    zx_handle_t handle;
    zx::unowned_channel ch(channel);
    zx_status_t st = ch->read(0, &request_, &handle, sizeof(request_), 1, &bytes_in, &handles_in);
    if (st != ZX_OK) {
        return ZX_ERR_INTERNAL;
    }

    if (bytes_in != sizeof(request_)) {
        return ZX_ERR_INTERNAL;
    }

    switch (request_.op) {
    case PCI_OP_CONFIG_READ:
        return RpcConfigRead(ch);
        break;
    case PCI_OP_CONFIG_WRITE:
        return RpcConfigWrite(ch);
        break;
    case PCI_OP_CONNECT_SYSMEM:
        return RpcConfigWrite(ch);
        break;
    case PCI_OP_ENABLE_BUS_MASTER:
        return RpcEnableBusMaster(ch);
        break;
    case PCI_OP_GET_AUXDATA:
        return RpcGetAuxdata(ch);
        break;
    case PCI_OP_GET_BAR:
        return RpcGetBar(ch);
        break;
    case PCI_OP_GET_BTI:
        return RpcGetBti(ch);
        break;
    case PCI_OP_GET_DEVICE_INFO:
        return RpcGetDeviceInfo(ch);
        break;
    case PCI_OP_GET_NEXT_CAPABILITY:
        return RpcGetNextCapability(ch);
        break;
    case PCI_OP_MAP_INTERRUPT:
        return RpcMapInterrupt(ch);
        break;
    case PCI_OP_QUERY_IRQ_MODE:
        return RpcQueryIrqMode(ch);
        break;
    case PCI_OP_RESET_DEVICE:
        return RpcResetDevice(ch);
        break;
    case PCI_OP_SET_IRQ_MODE:
        return RpcSetIrqMode(ch);
        break;
    case PCI_OP_MAX:
    case PCI_OP_INVALID: {
        return RpcReply(ch, ZX_ERR_INVALID_ARGS);
    }
    };

    return ZX_OK;
}

// Utility method to handle setting up the payload to return to the proxy and common
// error situations.
zx_status_t Device::RpcReply(const zx::unowned_channel& ch, zx_status_t st,
                             zx_handle_t* handles, const uint32_t handle_cnt) {
    response_.op = request_.op;
    response_.txid = request_.txid;
    response_.ret = st;
    return ch->write(0, &response_, sizeof(response_), handles, handle_cnt);
}

zx_status_t Device::RpcConfigRead(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcConfigWrite(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcEnableBusMaster(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcGetAuxdata(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcGetBar(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcGetBti(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcGetDeviceInfo(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcGetNextCapability(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcMapInterrupt(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcQueryIrqMode(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcResetDevice(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

zx_status_t Device::RpcSetIrqMode(const zx::unowned_channel& ch) {
    RPC_UNIMPLEMENTED;
}

} // namespace pci
