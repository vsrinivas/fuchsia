// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-s912/s912-hw.h>
#include "vim.h"

static const pbus_mmio_t vim_canvas_mmios[] = {
    {
        .base =     S912_DMC_REG_BASE,
        .length =   S912_DMC_REG_LENGTH,
    },
};

static const pbus_bti_t vim_canvas_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_CANVAS,
    },
};

static const pbus_dev_t canvas_dev = {
    .name = "canvas",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_CANVAS,
    .mmio_list = vim_canvas_mmios,
    .mmio_count = countof(vim_canvas_mmios),
    .bti_list = vim_canvas_btis,
    .bti_count = countof(vim_canvas_btis),
};

zx_status_t vim2_canvas_init(vim_bus_t* bus) {
    zx_status_t status = pbus_protocol_device_add(&bus->pbus, ZX_PROTOCOL_AMLOGIC_CANVAS,
                                                  &canvas_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_canvas_init: pbus_protocol_device_add Canvas failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}
