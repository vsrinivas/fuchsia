// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-clk.h>

#include "mt8167.h"

namespace {

constexpr pbus_mmio_t thermal_mmios[] = {
    {
        .base = MT8167_THERMAL_BASE,
        .length = MT8167_THERMAL_SIZE,
    },
    {
        .base = MT8167_FUSE_BASE,
        .length = MT8167_FUSE_SIZE,
    }
};

constexpr pbus_clk_t thermal_clks[] = {
    {
        .clk = board_mt8167::kClkThermal
    },
    {
        .clk = board_mt8167::kClkAuxAdc
    }
};

const pbus_dev_t thermal_dev = []() {
    pbus_dev_t thermal_dev = {};
    thermal_dev.name = "thermal";
    thermal_dev.vid = PDEV_VID_MEDIATEK;
    thermal_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    thermal_dev.did = PDEV_DID_MEDIATEK_THERMAL;
    thermal_dev.mmio_list = thermal_mmios;
    thermal_dev.mmio_count = countof(thermal_mmios);
    thermal_dev.clk_list = thermal_clks;
    thermal_dev.clk_count = countof(thermal_clks);
    thermal_dev.bti_count = 0;
    thermal_dev.metadata_count = 0;
    thermal_dev.irq_count = 0;
    thermal_dev.gpio_count = 0;
    return thermal_dev;
}();

}  // namespace

namespace board_mt8167 {

zx_status_t Mt8167::ThermalInit() {
    zx_status_t status = pbus_.DeviceAdd(&thermal_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd thermal failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
