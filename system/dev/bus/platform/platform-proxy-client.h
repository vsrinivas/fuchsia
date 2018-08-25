// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/platform-proxy.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include "platform-proxy.h"

namespace platform_bus {

class ProxyClient;
using ProxyClientType = ddk::Device<ProxyClient>;

class ProxyClient : public ProxyClientType,
                    public ddk::PlatformProxyProtocol<ProxyClient> {
public:
    explicit ProxyClient(uint32_t proto_id, zx_device_t* parent, fbl::RefPtr<PlatformProxy> proxy)
        :  ProxyClientType(parent), proto_id_(proto_id), proxy_(proxy) {}

    static zx_status_t Create(uint32_t proto_id, zx_device_t* parent,
                              fbl::RefPtr<PlatformProxy> proxy);

    // Device protocol implementation.
    void DdkRelease();

    // Platform proxy protocol implementation.
     zx_status_t RegisterProtocol(uint32_t proto_id, const void* protocol);
     zx_status_t Proxy(platform_proxy_args_t* args);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(ProxyClient);

    uint32_t proto_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

} // namespace platform_bus
