// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arm-isp.h"
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <memory>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/types.h>

namespace camera {

// static
zx_status_t ArmIspDevice::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto isp_device = std::unique_ptr<ArmIspDevice>(new (&ac) ArmIspDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = isp_device->DdkAdd("arm-isp");
    if (status != ZX_OK) {
        zxlogf(ERROR, "arm-isp: Could not create arm-isp device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "arm-isp: Added arm-isp device\n");
    }

    // isp_device intentionally leaked as it is now held by DevMgr.
    __UNUSED auto ptr = isp_device.release();

    return status;
}

ArmIspDevice::~ArmIspDevice() {
}

void ArmIspDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void ArmIspDevice::DdkRelease() {
    delete this;
}

void ArmIspDevice::ShutDown() {
}

} // namespace camera
