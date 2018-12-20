// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/mmio.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>
#include <soc/mt8167/mt8167-sdmmc.h>

#include "mt8167.h"

namespace {

constexpr uint16_t kPullUp   = 0;
constexpr uint16_t kPullDown = 1;

constexpr uint16_t kPull10k  = 1;
constexpr uint16_t kPull50k  = 2;

constexpr uintptr_t kGpioBaseAligned = fbl::round_down<uintptr_t, uintptr_t>(MT8167_MSDC2_GPIO_BASE,
                                                                             PAGE_SIZE);
constexpr size_t kGpioOffset = MT8167_MSDC2_GPIO_BASE - kGpioBaseAligned;
constexpr size_t kGpioSizeAligned = fbl::round_up<size_t, size_t>(
    kGpioOffset + MT8167_MSDC2_GPIO_SIZE, PAGE_SIZE);

constexpr uint32_t kFifoDepth = 128;
constexpr uint32_t kSrcClkFreq = 188000000;

}  // namespace

namespace board_mt8167 {

class PuPdCtrl4 : public hwreg::RegisterBase<PuPdCtrl4, uint16_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PuPdCtrl4>(kGpioOffset); }

    DEF_BIT(14, msdc2_dat2_pupd);
    DEF_FIELD(13, 12, msdc2_dat2_pull);

    DEF_BIT(10, msdc2_dat1_pupd);
    DEF_FIELD(9, 8, msdc2_dat1_pull);

    DEF_BIT(6, msdc2_dat0_pupd);
    DEF_FIELD(5, 4, msdc2_dat0_pull);
};

class PuPdCtrl5 : public hwreg::RegisterBase<PuPdCtrl5, uint16_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PuPdCtrl5>(kGpioOffset + 0x10); }

    DEF_BIT(10, msdc2_cmd_pupd);
    DEF_FIELD(9, 8, msdc2_cmd_pull);

    DEF_BIT(6, msdc2_clk_pupd);
    DEF_FIELD(5, 4, msdc2_clk_pull);

    DEF_BIT(2, msdc2_dat3_pupd);
    DEF_FIELD(1, 0, msdc2_dat3_pull);
};

zx_status_t Mt8167::SdioInit() {
    static const pbus_mmio_t sdio_mmios[] = {
        {
            .base = MT8167_MSDC2_BASE,
            .length = MT8167_MSDC2_SIZE,
        },
    };

    static const pbus_bti_t sdio_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_SDIO,
        }
    };

    static const MtkSdmmcConfig sdio_config = {
        .fifo_depth = kFifoDepth,
        .src_clk_freq = kSrcClkFreq
    };

    static const pbus_metadata_t sdio_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &sdio_config,
            .data_size = sizeof(sdio_config)
        }
    };

    static const pbus_irq_t sdio_irqs[] = {
        {
            .irq = MT8167_IRQ_MSDC2,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
        }
    };

    static const pbus_gpio_t sdio_gpios[] = {
        {
            .gpio = MT8167_GPIO_MT7668_PMU_EN
        }
    };

    pbus_dev_t sdio_dev = {};
    sdio_dev.name = "sdio";
    sdio_dev.vid = PDEV_VID_MEDIATEK;
    sdio_dev.did = PDEV_DID_MEDIATEK_SDIO;
    sdio_dev.mmio_list = sdio_mmios;
    sdio_dev.mmio_count = countof(sdio_mmios);
    sdio_dev.bti_list = sdio_btis;
    sdio_dev.bti_count = countof(sdio_btis);
    sdio_dev.metadata_list = sdio_metadata;
    sdio_dev.metadata_count = countof(sdio_metadata);
    sdio_dev.irq_list = sdio_irqs;
    sdio_dev.irq_count = countof(sdio_irqs);
    sdio_dev.gpio_list = sdio_gpios;
    sdio_dev.gpio_count = countof(sdio_gpios);

    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> gpio_mmio;
    zx_status_t status = ddk::MmioBuffer::Create(kGpioBaseAligned, kGpioSizeAligned, *root_resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to set MSDC2 GPIOs: %d\n", __FUNCTION__, status);
        return status;
    }

    // MSDC2 pins are not configured by the bootloader. Set the clk pin to 50k pull-down, all others
    // to 10k pull-up to match the device tree settings.
    PuPdCtrl4::Get()
        .ReadFrom(&(*gpio_mmio))
        .set_msdc2_dat2_pupd(kPullUp)
        .set_msdc2_dat2_pull(kPull10k)
        .set_msdc2_dat1_pupd(kPullUp)
        .set_msdc2_dat1_pull(kPull10k)
        .set_msdc2_dat0_pupd(kPullUp)
        .set_msdc2_dat0_pull(kPull10k)
        .WriteTo(&(*gpio_mmio));

    PuPdCtrl5::Get()
        .ReadFrom(&(*gpio_mmio))
        .set_msdc2_cmd_pupd(kPullUp)
        .set_msdc2_cmd_pull(kPull10k)
        .set_msdc2_clk_pupd(kPullDown)
        .set_msdc2_clk_pull(kPull50k)
        .set_msdc2_dat3_pupd(kPullUp)
        .set_msdc2_dat3_pull(kPull10k)
        .WriteTo(&(*gpio_mmio));

    if ((status = pbus_.DeviceAdd(&sdio_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd MSDC2 failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
