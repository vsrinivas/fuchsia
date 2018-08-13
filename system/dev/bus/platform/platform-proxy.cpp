// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/usb-mode-switch.h>

#include "platform-proxy-device.h"

// The implementation of the platform bus protocol in this file is for use by
// drivers that exist in a proxy devhost and communicate with the platform bus
// over an RPC channel.
//
// More information can be found at the top of platform-device.c.

namespace platform_bus {

zx_status_t PlatformProxy::Rpc(rpc_req_header_t* req, uint32_t req_length, rpc_rsp_header_t* resp,
                               uint32_t resp_length, zx_handle_t* in_handles,
                               uint32_t in_handle_count, zx_handle_t* out_handles,
                               uint32_t out_handle_count, uint32_t* out_actual) {
    uint32_t resp_size, handle_count;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = req_length,
        .wr_num_handles = in_handle_count,
        .rd_num_bytes = resp_length,
        .rd_num_handles = out_handle_count,
    };
    auto status = rpc_channel_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
        return status;
    } else if (resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "PlatformProxy::Rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (handle_count != out_handle_count) {
        zxlogf(ERROR, "PlatformProxy::Rpc handle count %u expected %u\n", handle_count,
               out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    status = resp->status;
    if (out_actual) {
        *out_actual = resp_size;
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

zx_status_t PlatformProxy::Create(zx_device_t* parent, zx_handle_t rpc_channel) {
    fbl::AllocChecker ac;

    auto proxy = fbl::MakeRefCountedChecked<PlatformProxy>(&ac, rpc_channel);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return ProxyDevice::Create(parent, proxy);
}

} // namespace platform_bus

zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    return platform_bus::PlatformProxy::Create(parent, rpc_channel);
}
