// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <lib/zx/channel.h>

namespace component {

class ComponentProxy;
// TODO(teisenbe): Add support for get_protocol and proxy it through
using ComponentProxyBase = ddk::Device<ComponentProxy, ddk::Unbindable>;

class ComponentProxy : public ComponentProxyBase {
public:
    ComponentProxy(zx_device_t* parent, zx::channel rpc)
        : ComponentProxyBase(parent), rpc_(std::move(rpc)) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name,
                              const char* args, zx_handle_t raw_rpc);

    zx_status_t DdkRxrpc(zx_handle_t channel);
    void DdkUnbind();
    void DdkRelease();
private:
    zx::channel rpc_;
};

} // namespace component
