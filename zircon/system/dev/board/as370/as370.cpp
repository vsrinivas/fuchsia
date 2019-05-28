// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <zircon/threads.h>

namespace board_as370 {

zx_status_t As370::Create(void* ctx, zx_device_t* parent) {
    ddk::PBusProtocolClient pbus(parent);
    if (!pbus.is_valid()) {
        zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PBUS\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }

    fbl::AllocChecker ac;
    auto board = fbl::make_unique_checked<As370>(&ac, parent, pbus);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = board->DdkAdd("as370", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed %d\n", __func__, status);
        return status;
    }

    if ((status = board->Start()) != ZX_OK) {
        return status;
    }

    __UNUSED auto* dummy = board.release();
    return ZX_OK;
}

zx_status_t As370::Start() {
    auto cb = [](void* arg) -> int { return reinterpret_cast<As370*>(arg)->Thread(); };
    int rc = thrd_create_with_name(&thread_, cb, this, "as370-start-thread");
    return thrd_status_to_zx_status(rc);
}

int As370::Thread() {
    if (GpioInit() != ZX_OK) {
        zxlogf(ERROR, "%s: GpioInit() failed\n", __func__);
        return thrd_error;
    }

    return 0;
}

}  // namespace board_as370

static constexpr zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = board_as370::As370::Create;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(as370, driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_SYNAPTICS_AS370),
ZIRCON_DRIVER_END(as370)
