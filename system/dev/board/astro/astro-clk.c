// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

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

static const pbus_dev_t clk_dev = {
    .name = "astro-clk",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_G12A_CLK,
    .mmio_list = clk_mmios,
    .mmio_count = countof(clk_mmios),
};

zx_status_t aml_clk_init(aml_bus_t* bus) {
    zx_status_t status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_CLK, &clk_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_clk_init: pbus_protocol_device_add failed, st = %d\n", status);
        return status;
    }

    return ZX_OK;
}
