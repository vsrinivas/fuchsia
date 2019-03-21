// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component-proxy.h"

#include <memory>

namespace component {

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

const zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.create = ComponentProxy::Create;
    return ops;
}();

} // namespace component

ZIRCON_DRIVER_BEGIN(component_proxy, component::driver_ops, "zircon", "0.1", 1)
// Unmatchable.  This is loaded via the proxy driver mechanism instead of the binding process
BI_ABORT()
ZIRCON_DRIVER_END(component_proxy)
