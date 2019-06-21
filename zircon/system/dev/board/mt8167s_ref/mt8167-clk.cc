// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::ClkInit() {
    static const pbus_mmio_t clk_mmios[] = {
        {
            .base = MT8167_XO_BASE,
            .length = MT8167_XO_SIZE
        }
    };

    pbus_dev_t clk_dev = {};
    clk_dev.name = "clk";
    clk_dev.vid = PDEV_VID_MEDIATEK;
    clk_dev.did = PDEV_DID_MEDIATEK_CLK;
    clk_dev.mmio_list = clk_mmios;
    clk_dev.mmio_count = countof(clk_mmios);
    clk_dev.bti_count = 0;
    clk_dev.metadata_count = 0;
    clk_dev.irq_count = 0;
    clk_dev.gpio_count = 0;

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clk_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd clk failed %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
