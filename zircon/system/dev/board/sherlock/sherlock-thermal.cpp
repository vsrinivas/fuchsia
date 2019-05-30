// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <hw/reg.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr uint32_t kPwmDFn = 3;

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = T931_TEMP_SENSOR_BASE,
        .length = T931_TEMP_SENSOR_LENGTH,
    },
    {
        .base = T931_GPIO_A0_BASE,
        .length = T931_GPIO_AO_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    {
        .base = T931_AO_PWM_CD_BASE,
        .length = T931_AO_PWM_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = T931_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_bti_t thermal_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_THERMAL,
    },
};

static const pbus_clk_t thermal_clk_gates[] = {
    {
        .clk = G12B_CLK_SYS_PLL_DIV16,
    },
    {
        .clk = G12B_CLK_SYS_CPU_CLK_DIV16,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(uint32_t temp_c,
                                                                    int32_t cpu_opp,
                                                                    int32_t gpu_opp) {
    constexpr uint32_t kHysteresis = 2;

    return {
        .up_temp = temp_c + kHysteresis,
        .down_temp = temp_c - kHysteresis,
        .fan_level = 0,
        .big_cluster_dvfs_opp = cpu_opp,
        .little_cluster_dvfs_opp = 0,
        .gpu_clk_freq_source = gpu_opp,
    };
}

/*
 * PASSIVE COOLING - For Astro, we have DVFS support added
 * Below is the operating point information for Big cluster
 * Operating point 0  - Freq 0.1000 Ghz Voltage 0.7310 V
 * Operating point 1  - Freq 0.2500 Ghz Voltage 0.7310 V
 * Operating point 2  - Freq 0.5000 Ghz Voltage 0.7310 V
 * Operating point 3  - Freq 0.6670 Ghz Voltage 0.7310 V
 * Operating point 4  - Freq 1.0000 Ghz Voltage 0.7310 V
 * Operating point 5  - Freq 1.2000 Ghz Voltage 0.7310 V
 * Operating point 6  - Freq 1.3980 Ghz Voltage 0.7610 V
 * Operating point 7  - Freq 1.5120 Ghz Voltage 0.7910 V
 * Operating point 8  - Freq 1.6080 Ghz Voltage 0.8310 V
 * Operating point 9  - Freq 1.7040 Ghz Voltage 0.8610 V
 * Operating point 10 - Freq 1.8960 Ghz Voltage 0.9810 V
 *
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 */

// clang-format off

// NOTE: This is a very trivial policy, no data backing it up
// As we do more testing this policy can evolve.
static fuchsia_hardware_thermal_ThermalDeviceInfo aml_sherlock_config = {
    .active_cooling                     = false,
    .passive_cooling                    = true,
    .gpu_throttling                     = true,
    .num_trip_points                    = 7,
    .big_little                         = false,
    .critical_temp                      = 102,
    .trip_point_info                    = {
        TripPoint(55, 10, 4),
        TripPoint(75, 9, 4),
        TripPoint(80, 7, 3),
        TripPoint(90, 6, 3),
        TripPoint(95, 5, 3),
        TripPoint(100, 4, 2),
    },
    .opps = {},
};

// clang-format on
static aml_opp_info_t aml_opp_info = {
    .opps = {
        {
            // 0
            .freq_hz = 100000000,
            .volt_mv = 731000,
        },
        {
            // 1
            .freq_hz = 250000000,
            .volt_mv = 731000,
        },
        {
            // 2
            .freq_hz = 500000000,
            .volt_mv = 731000,
        },
        {
            // 3
            .freq_hz = 667000000,
            .volt_mv = 731000,
        },
        {
            // 4
            .freq_hz = 1000000000,
            .volt_mv = 731000,
        },
        {
            // 5
            .freq_hz = 1200000000,
            .volt_mv = 731000,
        },
        {
            // 6
            .freq_hz = 1398000000,
            .volt_mv = 761000,
        },
        {
            // 7
            .freq_hz = 1512000000,
            .volt_mv = 791000,
        },
        {
            // 8
            .freq_hz = 1608000000,
            .volt_mv = 831000,
        },
        {
            // 9
            .freq_hz = 1704000000,
            .volt_mv = 861000,
        },
        {
            // 10
            .freq_hz = 1896000000,
            .volt_mv = 981000,
        },
    },
    .voltage_table = {
        {1022000, 0},
        {1011000, 3},
        {1001000, 6},
        {991000, 10},
        {981000, 13},
        {971000, 16},
        {961000, 20},
        {951000, 23},
        {941000, 26},
        {931000, 30},
        {921000, 33},
        {911000, 36},
        {901000, 40},
        {891000, 43},
        {881000, 46},
        {871000, 50},
        {861000, 53},
        {851000, 56},
        {841000, 60},
        {831000, 63},
        {821000, 67},
        {811000, 70},
        {801000, 73},
        {791000, 76},
        {781000, 80},
        {771000, 83},
        {761000, 86},
        {751000, 90},
        {741000, 93},
        {731000, 96},
        {721000, 100},
    },
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = &aml_sherlock_config,
        .data_size = sizeof(aml_sherlock_config),
    },
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &aml_opp_info,
        .data_size = sizeof(aml_opp_info),
    },
};

static pbus_dev_t thermal_dev = []() {
    pbus_dev_t dev;
    dev.name = "aml-thermal";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_S905D2;
    dev.did = PDEV_DID_AMLOGIC_THERMAL;
    dev.mmio_list = thermal_mmios;
    dev.mmio_count = countof(thermal_mmios);
    dev.clk_list = thermal_clk_gates;
    dev.clk_count = countof(thermal_clk_gates);
    dev.irq_list = thermal_irqs;
    dev.irq_count = countof(thermal_irqs);
    dev.bti_list = thermal_btis;
    dev.bti_count = countof(thermal_btis);
    dev.metadata_list = thermal_metadata;
    dev.metadata_count = countof(thermal_metadata);
    return dev;
}();

} // namespace

zx_status_t Sherlock::ThermalInit() {
    // Configure the GPIO to be Output & set it to alternate
    // function 3 which puts in PWM_D mode.
    gpio_impl_.SetAltFunction(T931_GPIOE(1), kPwmDFn);

    zx_status_t status = gpio_impl_.ConfigOut(T931_GPIOE(1), 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ConfigOut failed: %d\n", __func__, status);
        return status;
    }

    status = pbus_.DeviceAdd(&thermal_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return status;
}

} // namespace sherlock
