// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

static const pbus_mmio_t clk_mmios[] = {
    {
        .base = S912_HIU_BASE,
        .length = S912_HIU_LENGTH,
    },
};

static const pbus_dev_t clk_dev = {
    .name = "vim-clk",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S912,
    .did = PDEV_DID_AMLOGIC_AXG_CLK,
    .mmio_list = clk_mmios,
    .mmio_count = countof(clk_mmios),
};

zx_status_t vim_clk_init(vim_bus_t* bus) {
    zx_status_t status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_CLK, &clk_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_clk_init: pbus_protocol_device_add failed, st = %d\n", status);
        return status;
    }

    return ZX_OK;
}
