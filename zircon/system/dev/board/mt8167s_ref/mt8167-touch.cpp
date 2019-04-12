// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <lib/mmio/mmio.h>
#include <fbl/algorithm.h>
#include <hwreg/bitfields.h>
#include <lib/focaltech/focaltech.h>
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
    if (board_info_.vid != PDEV_VID_GOOGLE || board_info_.pid != PDEV_PID_CLEO) {
        return ZX_OK;
    }

    static constexpr uint32_t kDeviceId = FOCALTECH_DEVICE_FT6336;

    static const pbus_metadata_t touch_metadata[] = {
        {
            .type = DEVICE_METADATA_PRIVATE,
            .data_buffer = &kDeviceId,
            .data_size = sizeof(kDeviceId)
        },
    };

    pbus_dev_t touch_dev = {};
    touch_dev.name = "touch";
    touch_dev.vid = PDEV_VID_GENERIC;
    touch_dev.did = PDEV_DID_FOCALTOUCH;
    touch_dev.metadata_list = touch_metadata;
    touch_dev.metadata_count = countof(touch_metadata);

    // Composite binding rules for focaltech touch driver.
    constexpr zx_bind_inst_t root_match[] = {
        BI_MATCH(),
    };
    constexpr zx_bind_inst_t ft_i2c_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
        BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
        BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x38),
    };
    constexpr zx_bind_inst_t gpio_int_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_INT),
    };
    constexpr zx_bind_inst_t gpio_reset_match[] = {
        BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
        BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO_TOUCH_RST),
    };
    const device_component_part_t ft_i2c_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(ft_i2c_match), ft_i2c_match },
    };
    const device_component_part_t gpio_int_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(gpio_int_match), gpio_int_match },
    };
    const device_component_part_t gpio_reset_component[] = {
        { fbl::count_of(root_match), root_match },
        { fbl::count_of(gpio_reset_match), gpio_reset_match },
    };
    const device_component_t ft_components[] = {
        { fbl::count_of(ft_i2c_component), ft_i2c_component },
        { fbl::count_of(gpio_int_component), gpio_int_component },
        { fbl::count_of(gpio_reset_component), gpio_reset_component },
    };

    zx::unowned_resource root_resource(get_root_resource());
    std::optional<ddk::MmioBuffer> pmic_mmio;
    auto status = ddk::MmioBuffer::Create(kPmicBaseAligned, kPmicSizeAligned, *root_resource,
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

    // platform device protocol is only needed to provide metadata to the driver.
    // TODO(voydanoff) remove pdev after we have a better way to provide metadata to composite
    // devices.
    if ((status = pbus_.CompositeDeviceAdd(&touch_dev, ft_components, fbl::count_of(ft_components),
                                           UINT32_MAX)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to add touch device: %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
