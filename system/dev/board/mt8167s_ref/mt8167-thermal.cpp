// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>
#include <zircon/device/thermal.h>

#include "mt8167.h"

namespace {

constexpr pbus_mmio_t thermal_mmios[] = {
    {
        .base = MT8167_THERMAL_BASE,
        .length = MT8167_THERMAL_SIZE
    },
    {
        .base = MT8167_FUSE_BASE,
        .length = MT8167_FUSE_SIZE
    },
    {
        .base = MT8167_AP_MIXED_SYS_BASE,
        .length = MT8167_AP_MIXED_SYS_SIZE
    },
    {
        .base = MT8167_PMIC_WRAP_BASE,
        .length = MT8167_PMIC_WRAP_SIZE
    }
};

constexpr pbus_clk_t thermal_clks[] = {
    {
        .clk = board_mt8167::kClkThermal
    },
    {
        .clk = board_mt8167::kClkAuxAdc
    },
    {
        .clk = board_mt8167::kClkPmicWrapAp
    },
    {
        .clk = board_mt8167::kClkPmicWrap26M
    }
};

constexpr thermal_device_info_t thermal_dev_info = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp = 0,
    .trip_point_info = {},
    .opps = {
        [BIG_CLUSTER_POWER_DOMAIN] = {
            // See section 3.6 (MTCMOS Domains) of the functional specification document.
            .opp = {
                [0] = {
                    .freq_hz = 598000000,
                    .volt_mv = 1150000
                },
                [1] = {
                    .freq_hz = 747500000,
                    .volt_mv = 1150000
                },
                [2] = {
                    .freq_hz = 1040000000,
                    .volt_mv = 1200000
                },
                [3] = {
                    .freq_hz = 1196000000,
                    .volt_mv = 1250000
                },
                [4] = {
                    .freq_hz = 1300000000,
                    .volt_mv = 1300000
                },
            },
            .latency = 0,
            .count = 5
        },
        [LITTLE_CLUSTER_POWER_DOMAIN] = {
            .opp = {},
            .latency = 0,
            .count = 0
        }
    }
};

constexpr pbus_metadata_t thermal_metadata[] = {
    {
        .type = THERMAL_CONFIG_METADATA,
        .data_buffer = &thermal_dev_info,
        .data_size = sizeof(thermal_dev_info)
    },
};

const pbus_dev_t thermal_dev = []() {
    pbus_dev_t thermal_dev = {};
    thermal_dev.name = "thermal";
    thermal_dev.vid = PDEV_VID_MEDIATEK;
    thermal_dev.did = PDEV_DID_MEDIATEK_THERMAL;
    thermal_dev.mmio_list = thermal_mmios;
    thermal_dev.mmio_count = countof(thermal_mmios);
    thermal_dev.clk_list = thermal_clks;
    thermal_dev.clk_count = countof(thermal_clks);
    thermal_dev.metadata_list = thermal_metadata;
    thermal_dev.metadata_count = countof(thermal_metadata);
    thermal_dev.bti_count = 0;
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
