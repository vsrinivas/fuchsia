// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sysdev.h"

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/types.h>

namespace {

class Sysdev;
using SysdevType = ddk::Device<Sysdev>;

class Sysdev : public SysdevType {
public:
    explicit Sysdev(zx_device_t* device) : SysdevType(device) { }

    static zx_status_t Create(zx_device_t* parent, const char* name, zx::channel items_svc);

    // Device protocol implementation.
    void DdkRelease() {
        // sysdev should never have its release called.
        ZX_ASSERT_MSG(false, "Sysdev::DdkRelease() invoked!\n");
    }
};

zx_status_t Sysdev::Create(zx_device_t* parent, const char* name, zx::channel items_svc) {
    auto sysdev = std::make_unique<Sysdev>(parent);

    zx_status_t status = sysdev->DdkAdd("sys", DEVICE_ADD_NON_BINDABLE,
                                        nullptr /* props */, 0 /* prop_count */);
    if (status != ZX_OK) {
        return status;
    }

    // Now owned by devmgr.
    __UNUSED auto ptr = sysdev.release();

    return ZX_OK;
}

} // namespace

zx_status_t test_sysdev_create(void* ctx, zx_device_t* parent, const char* name,
                               const char* args, zx_handle_t items_svc_handle) {
    zx::channel items_svc(items_svc_handle);
    return Sysdev::Create(parent, name, std::move(items_svc));
}
