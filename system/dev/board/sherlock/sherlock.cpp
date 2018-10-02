// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sherlock.h"

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

namespace sherlock {

zx_status_t Sherlock::Create(zx_device_t* parent) {
    platform_bus_protocol_t pbus;

    auto status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto board = fbl::make_unique_checked<Sherlock>(&ac, parent, &pbus);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = board->DdkAdd("sherlock", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();

    // Start up our protocol helpers and platform devices.
    board->Start();

    return ZX_OK;
}

void Sherlock::Start() {
    // TODO: Start various drivers here.
}

void Sherlock::DdkRelease() {
    delete this;
}

} // namespace sherlock

zx_status_t sherlock_bind(void* ctx, zx_device_t* parent) {
    return sherlock::Sherlock::Create(parent);
}
