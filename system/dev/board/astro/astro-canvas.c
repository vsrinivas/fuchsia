// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include <limits.h>

#include "astro.h"

static const pbus_mmio_t astro_canvas_mmios[] = {
    {
        .base = S905D2_DMC_BASE,
        .length = S905D2_DMC_LENGTH,
    },
};

static const pbus_bti_t astro_canvas_btis[] = {
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
    .mmios = astro_canvas_mmios,
    .mmio_count = countof(astro_canvas_mmios),
    .btis = astro_canvas_btis,
    .bti_count = countof(astro_canvas_btis),
};

zx_status_t aml_canvas_init(aml_bus_t* bus) {
    zx_status_t status = pbus_device_add(&bus->pbus, &canvas_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pbus_device_add canvas failed: %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}