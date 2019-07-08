// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t clk_mmios[] = {
    // CLK Registers
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    // CLK MSR block
    {
        .base = S905D2_MSR_CLK_BASE,
        .length = S905D2_MSR_CLK_LENGTH,
    },
};

static const pbus_dev_t clk_dev = []() {
    pbus_dev_t dev = {};
    dev.name = "astro-clk";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_S905D2;
    dev.did = PDEV_DID_AMLOGIC_G12A_CLK;
    dev.mmio_list = clk_mmios;
    dev.mmio_count = countof(clk_mmios);
    return dev;
}();

zx_status_t Astro::ClkInit() {
    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL,
                                                 &clk_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed, st = %d\n",
               __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace astro
