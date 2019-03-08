// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <lib/zx/channel.h>
#include <memory>

namespace {

class ComponentProxy;
// TODO(teisenbe): Add support for get_protocol and proxy it through
using ComponentProxyBase = ddk::Device<ComponentProxy, ddk::Unbindable>;

class ComponentProxy : public ComponentProxyBase {
public:
    ComponentProxy(zx_device_t* parent, zx::channel rpc);

    static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name,
                              const char* args, zx_handle_t raw_rpc);

    zx_status_t DdkRxrpc(zx_handle_t channel);
    void DdkUnbind();
    void DdkRelease();
private:
    zx::channel rpc_;
};

ComponentProxy::ComponentProxy(zx_device_t* parent, zx::channel rpc)
    : ComponentProxyBase(parent), rpc_(std::move(rpc)) {}

zx_status_t ComponentProxy::Create(void* ctx, zx_device_t* parent, const char* name,
                                   const char* args, zx_handle_t raw_rpc) {
    zx::channel rpc(raw_rpc);
    auto dev = std::make_unique<ComponentProxy>(parent, std::move(rpc));
    auto status = dev->DdkAdd("component-proxy", DEVICE_ADD_NON_BINDABLE);
    if (status == ZX_OK) {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

void ComponentProxy::DdkUnbind() {
    DdkRemove();
}

void ComponentProxy::DdkRelease() {
    delete this;
}

const zx_driver_ops_t component_proxy_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.create = ComponentProxy::Create;
    return ops;
}();

} // namespace

ZIRCON_DRIVER_BEGIN(component_proxy, component_proxy_driver_ops, "zircon", "0.1", 1)
// Unmatchable.  This is loaded via the proxy driver mechanism instead of the binding process
BI_ABORT()
ZIRCON_DRIVER_END(component_proxy)
