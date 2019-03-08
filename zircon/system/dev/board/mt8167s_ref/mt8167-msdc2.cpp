// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <lib/mmio/mmio.h>
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

zx_status_t Mt8167::Msdc2Init() {
    // MSDC2 is SD on Eagle, this will be supported later.
    if (board_info_.pid == PDEV_PID_EAGLE) {
        return ZX_OK;
    }

    static const pbus_mmio_t msdc2_mmios[] = {
        {
            .base = MT8167_MSDC2_BASE,
            .length = MT8167_MSDC2_SIZE,
        }
    };

    static const pbus_bti_t msdc2_btis[] = {
        {
            .iommu_index = 0,
            .bti_id = BTI_MSDC2,
        }
    };

    static const MtkSdmmcConfig msdc2_config = {
        .fifo_depth = kFifoDepth,
        .src_clk_freq = kSrcClkFreq,
        .is_sdio = true
    };

    static const pbus_metadata_t msdc2_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &msdc2_config,
            .data_size = sizeof(msdc2_config)
        }
    };

    static const pbus_irq_t msdc2_irqs[] = {
        {
            .irq = MT8167_IRQ_MSDC2,
            .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
        }
    };

    static const pbus_gpio_t msdc2_ref_gpios[] = {
        {
            .gpio = MT8167_GPIO_MT7668_PMU_EN
        }
    };

    static const pbus_gpio_t msdc2_cleo_gpios[] = {
        {
            .gpio = MT8167_GPIO_MT7668_PMU_EN
        },
        {
            .gpio = MT8167_CLEO_GPIO_HUB_PWR_EN
        }
    };

    const pbus_gpio_t* msdc2_gpios = msdc2_ref_gpios;
    size_t msdc2_gpio_count = countof(msdc2_ref_gpios);

    if (board_info_.pid == PDEV_PID_CLEO) {
        msdc2_gpios = msdc2_cleo_gpios;
        msdc2_gpio_count = countof(msdc2_cleo_gpios);
    }

    pbus_dev_t msdc2_dev = {};
    msdc2_dev.name = "sdio";
    msdc2_dev.vid = PDEV_VID_MEDIATEK;
    msdc2_dev.did = PDEV_DID_MEDIATEK_MSDC2;
    msdc2_dev.mmio_list = msdc2_mmios;
    msdc2_dev.mmio_count = countof(msdc2_mmios);
    msdc2_dev.bti_list = msdc2_btis;
    msdc2_dev.bti_count = countof(msdc2_btis);
    msdc2_dev.metadata_list = msdc2_metadata;
    msdc2_dev.metadata_count = countof(msdc2_metadata);
    msdc2_dev.irq_list = msdc2_irqs;
    msdc2_dev.irq_count = countof(msdc2_irqs);
    msdc2_dev.gpio_list = msdc2_gpios;
    msdc2_dev.gpio_count = msdc2_gpio_count;

    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> gpio_mmio;
    zx_status_t status = ddk::MmioBuffer::Create(kGpioBaseAligned, kGpioSizeAligned, *root_resource,
                                                 ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to set MSDC2 GPIOS: %d\n", __FUNCTION__, status);
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

    if ((status = pbus_.DeviceAdd(&msdc2_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd MSDC2 failed: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
