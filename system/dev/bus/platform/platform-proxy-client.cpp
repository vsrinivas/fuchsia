// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy-client.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/unique_ptr.h>

#include "platform-proxy.h"

namespace platform_bus {

zx_status_t ProxyClient::Create(uint32_t proto_id, zx_device_t* parent,
                                fbl::RefPtr<PlatformProxy> proxy) {
    fbl::AllocChecker ac;
    auto client = fbl::make_unique_checked<ProxyClient>(&ac, proto_id, parent, proxy);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    char name[ZX_DEVICE_NAME_MAX];
    snprintf(name, sizeof(name), "ProxyClient[%08x]", proto_id);

    zx_device_prop_t props[] = {
        {BIND_PLATFORM_PROTO, 0, proto_id},
    };

    auto status = client->DdkAdd(name, 0, props, countof(props));
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = client.release();
    return ZX_OK;
}

void ProxyClient::DdkRelease() {
    proxy_->UnregisterProtocol(proto_id_);
    delete this;
}

zx_status_t ProxyClient::RegisterProtocol(uint32_t proto_id, const void* protocol) {
    if (proto_id != proto_id_) {
        // We may allow drivers to implement multiple protocols in the future,
        // but for now require that the driver only proxy the one protocol we loaded it for.
        return ZX_ERR_ACCESS_DENIED;
    }
    return proxy_->RegisterProtocol(proto_id, protocol);

}

zx_status_t ProxyClient::Proxy(platform_proxy_args_t* args) {
    if (args->req->proto_id != proto_id_) {
        // We may allow drivers to implement multiple protocols in the future,
        // but for now require that the driver only proxy the one protocol we loaded it for.
        return ZX_ERR_ACCESS_DENIED;
    }
    return proxy_->Proxy(args);
}

} // namespace platform_bus
