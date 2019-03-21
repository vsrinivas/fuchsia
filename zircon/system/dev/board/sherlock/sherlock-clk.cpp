// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t clk_mmios[] = {
    // CLK Registers
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    // CLK MSR block
    {
        .base = T931_MSR_CLK_BASE,
        .length = T931_MSR_CLK_LENGTH,
    },
};

static pbus_dev_t clk_dev = []() {
    pbus_dev_t dev;
    dev.name = "sherlock-clk",
    dev.vid = PDEV_VID_AMLOGIC;
    dev.did = PDEV_DID_AMLOGIC_G12B_CLK;
    dev.mmio_list = clk_mmios;
    dev.mmio_count = countof(clk_mmios);
    return dev;
}();

zx_status_t Sherlock::ClkInit() {
    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
