// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/mmio.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace {

constexpr uintptr_t kPmicBaseAligned =
    fbl::round_down<uintptr_t, uintptr_t>(MT8167_PMIC_WRAP_BASE, PAGE_SIZE);
constexpr size_t kPmicOffset = MT8167_PMIC_WRAP_BASE - kPmicBaseAligned;
constexpr size_t kPmicSizeAligned =
    fbl::round_up<size_t, size_t>(kPmicOffset + MT8167_PMIC_WRAP_SIZE, PAGE_SIZE);

constexpr uint32_t kDigLdoCon7 = 0x285;
constexpr uint16_t kVgp1Enable = 0x8000;

}  // namespace

namespace board_mt8167 {

class PmicCmd : public hwreg::RegisterBase<PmicCmd, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<PmicCmd>(0xa0 + kPmicOffset); }

    DEF_BIT(31, write);
    DEF_FIELD(30, 16, addr);
    DEF_FIELD(15, 0, data);
};

class PmicReadData : public hwreg::RegisterBase<PmicReadData, uint32_t> {
public:
    static constexpr uint32_t kStateIdle  = 0;

    static auto Get() { return hwreg::RegisterAddr<PmicReadData>(0xa4 + kPmicOffset); }

    DEF_FIELD(18, 16, status);
};

zx_status_t Mt8167::TouchInit() {
    pdev_board_info_t info;
    zx_status_t status = pbus_.GetBoardInfo(&info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBoardInfo failed\n", __FILE__);
        return status;
    }

    if (info.vid != PDEV_VID_GOOGLE || info.pid != PDEV_PID_CLEO) {
        return ZX_OK;
    }

    static constexpr pbus_gpio_t touch_gpios[] = {
        {
            .gpio = MT8167_GPIO_TOUCH_INT
        },
        {
            .gpio = MT8167_GPIO_TOUCH_RST
        },
    };

    static constexpr pbus_i2c_channel_t touch_i2cs[] = {
        {
            .bus_id = 0,
            .address = 0x38
        },
    };

    pbus_dev_t touch_dev = {};
    touch_dev.name = "touch";
    touch_dev.vid = PDEV_VID_GENERIC;
    touch_dev.did = PDEV_DID_FOCALTOUCH;
    touch_dev.i2c_channel_list = touch_i2cs;
    touch_dev.i2c_channel_count = countof(touch_i2cs);
    touch_dev.gpio_list = touch_gpios;
    touch_dev.gpio_count = countof(touch_gpios);

    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> pmic_mmio;
    status = ddk::MmioBuffer::Create(kPmicBaseAligned, kPmicSizeAligned, *root_resource,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &pmic_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to enable VGP1 regulator: %d\n", __FUNCTION__, status);
        return status;
    }

    while (PmicReadData::Get().ReadFrom(&(*pmic_mmio)).status() != PmicReadData::kStateIdle) {}

    PmicCmd::Get()
        .FromValue(0)
        .set_write(1)
        .set_addr(kDigLdoCon7)
        .set_data(kVgp1Enable)
        .WriteTo(&(*pmic_mmio));

    if ((status = pbus_.DeviceAdd(&touch_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to add touch device: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
