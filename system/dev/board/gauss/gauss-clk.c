// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <soc/aml-a113/a113-hw.h>

#include "gauss.h"

static const pbus_mmio_t clk_mmios[] = {
    {
        .base = AXG_HIU_BASE_PHYS,
        .length = PAGE_SIZE,
    }
};

static const pbus_dev_t clk_dev = {
    .name = "a113-clk",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_A113,
    .did = PDEV_DID_AMLOGIC_AXG_CLK,
    .mmios = clk_mmios,
    .mmio_count = countof(clk_mmios),
};

zx_status_t gauss_clk_init(gauss_bus_t* bus) {
    zxlogf(INFO, "gauss_clk_init");
    zx_status_t st;

    st = pbus_device_add(&bus->pbus, &clk_dev, PDEV_ADD_PBUS_DEVHOST);
    if (st != ZX_OK) {
        zxlogf(ERROR, "gauss_clk_init: pbus_device_add failed, st = %d\n", st);
        return st;
    }

    st = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_CLK);
    if (st != ZX_OK) {
        zxlogf(ERROR, "gauss_clk_init: pbus_wait_protocol failed, st = %d\n", st);
        return st;
    }

    st = device_get_protocol(bus->parent, ZX_PROTOCOL_CLK, &bus->clk);
    if (st != ZX_OK) {
        zxlogf(ERROR, "gauss_clk_init: device_get_protocol failed, st = %d\n", st);
        return st;
    }

    return ZX_OK;
}