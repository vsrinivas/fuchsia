// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpt.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-sdmmc.h>

#include "mt8167.h"

namespace {

constexpr uint32_t kFifoDepth = 128;
constexpr uint32_t kSrcClkFreq = 188000000;

}  // namespace

namespace board_mt8167 {

zx_status_t Mt8167::Msdc1Init() {
    if (board_info_.pid != PDEV_PID_EAGLE) {
        return ZX_OK;
    }

    static const pbus_mmio_t msdc1_mmios[] = {
        {
            .base = MT8167_MSDC1_BASE,
            .length = MT8167_MSDC1_SIZE,
        },
    };

    static const pbus_bti_t msdc1_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_MSDC1,
        }
    };

    static const MtkSdmmcConfig msdc1_config = {
        .fifo_depth = kFifoDepth,
        .src_clk_freq = kSrcClkFreq,
        .is_sdio = true
    };

    static const pbus_metadata_t msdc1_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &msdc1_config,
            .data_size = sizeof(msdc1_config)
        },
        {
            .type = DEVICE_METADATA_GUID_MAP,
            .data_buffer = nullptr,
            .data_size = 0
        }
    };

    static const pbus_irq_t msdc1_irqs[] = {
        {
            .irq = MT8167_IRQ_MSDC1,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
        }
    };

    static const pbus_gpio_t msdc1_eagle_gpios[] = {
        {
            .gpio = MT8167_EAGLE_GPIO_MT6630_SYSRST
        },
        {
            .gpio = MT8167_EAGLE_GPIO_MT6630_PMU_EN
        }
    };

    pbus_dev_t msdc1_dev = {};
    msdc1_dev.name = "sdio";
    msdc1_dev.vid = PDEV_VID_MEDIATEK;
    msdc1_dev.did = PDEV_DID_MEDIATEK_MSDC1;
    msdc1_dev.mmio_list = msdc1_mmios;
    msdc1_dev.mmio_count = countof(msdc1_mmios);
    msdc1_dev.bti_list = msdc1_btis;
    msdc1_dev.bti_count = countof(msdc1_btis);
    msdc1_dev.metadata_list = msdc1_metadata;
    msdc1_dev.metadata_count = countof(msdc1_metadata);
    msdc1_dev.irq_list = msdc1_irqs;
    msdc1_dev.irq_count = countof(msdc1_irqs);
    msdc1_dev.gpio_list = msdc1_eagle_gpios;
    msdc1_dev.gpio_count = countof(msdc1_eagle_gpios);

    zx_status_t status = pbus_.DeviceAdd(&msdc1_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd MSDC1 failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
