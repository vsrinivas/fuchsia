// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>

#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

namespace board_mt8167 {

zx_status_t Mt8167::Create(zx_device_t* parent) {
    pbus_protocol_t pbus;

    auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto board = fbl::make_unique_checked<Mt8167>(&ac, parent, &pbus);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = board->DdkAdd("mt8167s_ref", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        return status;
    }

    // Start up our protocol helpers and platform devices.
    status = board->Start();
    if (status == ZX_OK) {
        // devmgr is now in charge of the device.
        __UNUSED auto* dummy = board.release();
    }
    return status;
}

int Mt8167::Thread() {
    if (SocInit() != ZX_OK) {
        zxlogf(ERROR, "SocInit() failed\n");
        return -1;
    }
    if (GpioInit() != ZX_OK) {
        zxlogf(ERROR, "GpioInit() failed\n");
        return -1;
    }
    if (I2cInit() != ZX_OK) {
        zxlogf(ERROR, "I2cInit() failed\n");
        return -1;
    }
    if (EmmcInit() != ZX_OK) {
        zxlogf(ERROR, "EmmcInit() failed\n");
        return -1;
    }
    if (DisplayInit() != ZX_OK) {
        zxlogf(ERROR, "DisplayInit() failed\n");
        return -1;
    }

    return 0;
}

zx_status_t Mt8167::Start() {
    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Mt8167*>(arg)->Thread();
                                   },
                                   this,
                                   "mt8167-start-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

void Mt8167::DdkRelease() {
    delete this;
}

} // namespace board_mt8167

zx_status_t mt8167_bind(void* ctx, zx_device_t* parent) {
    return board_mt8167::Mt8167::Create(parent);
}
