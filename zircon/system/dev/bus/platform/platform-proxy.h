// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>

#include "platform-proxy-device.h"
#include "proxy-protocol.h"

namespace platform_bus {

class ProxyDevice;

class PlatformProxy;
using PlatformProxyType = ddk::Device<PlatformProxy>;

// This is the main class for the proxy side platform bus driver.
// It handles RPC communication with the main platform bus driver in the root devhost.
class PlatformProxy : public PlatformProxyType, public fbl::RefCounted<PlatformProxy> {
public:
    static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name,
                              const char* args, zx_handle_t rpc_channel);

    // Device protocol implementation.
    void DdkRelease();

    zx_status_t Rpc(uint32_t device_id, const platform_proxy_req_t* req, size_t req_length,
                    platform_proxy_rsp_t* resp, size_t resp_length, const zx_handle_t* in_handles,
                    size_t in_handle_count, zx_handle_t* out_handles, size_t out_handle_count,
                    size_t* out_actual);

    inline zx_status_t Rpc(uint32_t device_id, const platform_proxy_req_t* req, size_t req_length,
                           platform_proxy_rsp_t* resp, size_t resp_length) {
        return Rpc(device_id, req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
    }

private:
    friend class fbl::RefPtr<PlatformProxy>;
    friend class fbl::internal::MakeRefCountedHelper<PlatformProxy>;

    explicit PlatformProxy(zx_device_t* parent, zx_handle_t rpc_channel)
        :  PlatformProxyType(parent), rpc_channel_(rpc_channel) {}

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformProxy);

    zx_status_t Init(zx_device_t* parent);

    const zx::channel rpc_channel_;
};

} // namespace platform_bus
