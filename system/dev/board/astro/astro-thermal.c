// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "astro.h"
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zircon/device/thermal.h>

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D2_TEMP_SENSOR_BASE,
        .length = S905D2_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_AO_PWM_CD_BASE,
        .length = S905D2_AO_PWM_LENGTH,
    }};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

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
 * GPU_CLK_FREQUENCY_SOURCE - // TOD(jbauman): (Put actual numbers below)
 */

// clang-format off
static thermal_device_info_t aml_vim2_config = {
    .active_cooling                     = false,
    .passive_cooling                    = true,
    .gpu_throttling                     = false,
    .num_trip_points                    = 11,
    .critical_temp                      = 130,
    .big_little                         = false,
    .trip_point_info                    = {
        // Below trip point info is dummy for now.
        // TOD(braval): (Put actual numbers below)
        {
            // This is the initial thermal setup of the device.
            // CPU freq set to a known stable MAX.
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 65,
            .down_temp                  = 63,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 70,
            .down_temp                  = 68,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 75,
            .down_temp                  = 73,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 82,
            .down_temp                  = 79,
            .big_cluster_dvfs_opp       = 5,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 87,
            .down_temp                  = 84,
            .big_cluster_dvfs_opp       = 4,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .up_temp                    = 92,
            .down_temp                  = 89,
            .big_cluster_dvfs_opp       = 3,
            .little_cluster_dvfs_opp    = 3,
        },
        {
            .up_temp                    = 96,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
        },
        {
            .up_temp                    = 96,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
        },
        {
            .up_temp                    = 96,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
        },
        {
            .up_temp                    = 96,
            .down_temp                  = 93,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
        },
    },
};

// clang-format on
static opp_info_t aml_opp_info = {
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
    // TODO(braval): Add Opp Table.
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = THERMAL_CONFIG_METADATA,
        .extra = 0,
        .data = &aml_vim2_config,
        .len = sizeof(aml_vim2_config),
    },
    {
        .type = VOLTAGE_DUTY_CYCLE_METADATA,
        .extra = 0,
        .data = &aml_opp_info,
        .len = sizeof(aml_opp_info),
    },
};

static pbus_dev_t thermal_dev = {
    .name = "aml-thermal",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_THERMAL,
    .mmios = thermal_mmios,
    .mmio_count = countof(thermal_mmios),
    .irqs = thermal_irqs,
    .irq_count = countof(thermal_irqs),
    .metadata = thermal_metadata,
    .metadata_count = countof(thermal_metadata),
};

zx_status_t aml_thermal_init(aml_bus_t* bus) {
    // Configure the GPIO to be Output & set it to alternate
    // function 3 which puts in PWM_D mode.
    zx_status_t status = gpio_config(&bus->gpio, S905D2_PWM_D, GPIO_DIR_OUT);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_config failed: %d\n", status);
        return status;
    }

    status = gpio_set_alt_function(&bus->gpio, S905D2_PWM_D, S905D2_PWM_D_FN);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_set_alt_function failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &thermal_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}
