// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::EmmcInit() {
    const pbus_mmio_t emmc_mmios[] = {
        {
            .base = MT8167_MSDC0_BASE,
            .length = MT8167_MSDC0_SIZE,
        }
    };

    const pbus_bti_t emmc_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_EMMC,
        }
    };

    static uint32_t msdc_fifo_depth = 128;

    static const pbus_metadata_t emmc_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data = &msdc_fifo_depth,
            .len = sizeof(msdc_fifo_depth)
        }
    };

    pbus_dev_t emmc_dev = {};
    emmc_dev.name = "emmc";
    emmc_dev.vid = PDEV_VID_MEDIATEK;
    emmc_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    emmc_dev.did = PDEV_DID_MEDIATEK_EMMC;
    emmc_dev.mmios = emmc_mmios;
    emmc_dev.mmio_count = countof(emmc_mmios);
    emmc_dev.btis = emmc_btis;
    emmc_dev.bti_count = countof(emmc_btis);
    emmc_dev.metadata = emmc_metadata;
    emmc_dev.metadata_count = countof(emmc_metadata);

    zx_status_t status = pbus_.DeviceAdd(&emmc_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
    }

    return status;
}

} // namespace board_mt8167
