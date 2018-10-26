// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
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

    static const guid_map_t guid_map[] = {
        { "boot_a", GUID_ZIRCON_A_VALUE },
        { "boot_b", GUID_ZIRCON_B_VALUE },
        { "userdata", GUID_FVM_VALUE }
    };
    static_assert(sizeof(guid_map) / sizeof(guid_map[0]) <= DEVICE_METADATA_GUID_MAP_MAX_ENTRIES);

    static const pbus_metadata_t emmc_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &msdc_fifo_depth,
            .data_size = sizeof(msdc_fifo_depth)
        },
        {
            .type = DEVICE_METADATA_GUID_MAP,
            .data_buffer = guid_map,
            .data_size = sizeof(guid_map)
        }
    };

    pbus_dev_t emmc_dev = {};
    emmc_dev.name = "emmc";
    emmc_dev.vid = PDEV_VID_MEDIATEK;
    emmc_dev.pid = PDEV_PID_MEDIATEK_8167S_REF;
    emmc_dev.did = PDEV_DID_MEDIATEK_EMMC;
    emmc_dev.mmio_list = emmc_mmios;
    emmc_dev.mmio_count = countof(emmc_mmios);
    emmc_dev.bti_list = emmc_btis;
    emmc_dev.bti_count = countof(emmc_btis);
    emmc_dev.metadata_list = emmc_metadata;
    emmc_dev.metadata_count = countof(emmc_metadata);

    zx_status_t status = pbus_.DeviceAdd(&emmc_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
    }

    return status;
}

} // namespace board_mt8167
