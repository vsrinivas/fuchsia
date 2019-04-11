// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>

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
    },
    {
        .base = MT8167_INFRACFG_BASE,
        .length = MT8167_INFRACFG_SIZE
    }
};

constexpr pbus_clk_t thermal_clks[] = {
    {
        .clk = board_mt8167::kClkThem
    },
    {
        .clk = board_mt8167::kClkAuxAdc
    },
    {
        .clk = board_mt8167::kClkPmicwrapAp
    },
    {
        .clk = board_mt8167::kClkPmicwrap26m
    }
};

constexpr pbus_irq_t thermal_irqs[] = {
    {
        .irq = MT8167_IRQ_PTP_THERM,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH
    }
};

constexpr uint32_t CToKTenths(uint32_t temp_c) {
    constexpr uint32_t kKelvinOffset = 2732;  // Units: 0.1 degrees C
    return (temp_c * 10) + kKelvinOffset;
}

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(uint32_t temp_c, int32_t opp) {
    constexpr uint32_t kHysteresis = 2;

    return {
        .up_temp = CToKTenths(temp_c + kHysteresis),
        .down_temp = CToKTenths(temp_c - kHysteresis),
        .fan_level = 0,
        .big_cluster_dvfs_opp = opp,
        .little_cluster_dvfs_opp = 0,
        .gpu_clk_freq_source = 0
    };
}

constexpr fuchsia_hardware_thermal_ThermalDeviceInfo thermal_dev_info = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 5,
    .big_little = false,
    .critical_temp = CToKTenths(120),
    .trip_point_info = {
        TripPoint(55, 4),
        TripPoint(65, 3),
        TripPoint(75, 2),
        TripPoint(85, 1),
        TripPoint(95, 0),
    },
    .opps = {
        [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = {
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
        [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] = {
            .opp = {},
            .latency = 0,
            .count = 0
        }
    }
};

constexpr pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
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
    thermal_dev.irq_list = thermal_irqs;
    thermal_dev.irq_count = countof(thermal_irqs);
    thermal_dev.bti_count = 0;
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
