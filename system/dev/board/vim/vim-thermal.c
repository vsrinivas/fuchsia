// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include <soc/aml-common/aml-thermal.h>
#include "vim.h"

static const pbus_gpio_t fanctl_gpios[] = {
    {
        .gpio = S912_GPIODV(14),
    },
    {
        .gpio = S912_GPIODV(15),
    }
};

static aml_thermal_config_t aml_vim2_config = {
    .active_cooling                 = true,
    .passive_cooling                = true,
    .gpu_throttling                 = false,
    .trip_point_count               = 8,
    .critical_temp                  = 81,
    .trip_point_info = {
        {
            // This is the initial thermal setup of the device
            // Fan set to OFF
            // CPU freq set to a known stable MAX
            .fan_level                  = 0,
            .big_cluster_dvfs_opp       = 6,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .fan_level                  = 1,
            .up_temp                    = 45,
            .down_temp                  = 43,
            .big_cluster_dvfs_opp       = -1,
            .little_cluster_dvfs_opp    = -1,
        },
        {
            .fan_level                  = 2,
            .up_temp                    = 55,
            .down_temp                  = 53,
            .big_cluster_dvfs_opp       = -1,
            .little_cluster_dvfs_opp    = -1,
        },
        {
            .fan_level                  = 3,
            .up_temp                    = 60,
            .down_temp                  = 58,
            .big_cluster_dvfs_opp       = -1,
            .little_cluster_dvfs_opp    = -1,
        },
        {
            .fan_level                  = -1,
            .up_temp                    = 70,
            .down_temp                  = 68,
            .big_cluster_dvfs_opp       = 5,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .fan_level                  = -1,
            .up_temp                    = 74,
            .down_temp                  = 73,
            .big_cluster_dvfs_opp       = 4,
            .little_cluster_dvfs_opp    = 4,
        },
        {
            .fan_level                  = -1,
            .up_temp                    = 77,
            .down_temp                  = 75,
            .big_cluster_dvfs_opp       = 3,
            .little_cluster_dvfs_opp    = 3,
        },
        {
            .fan_level                  = -1,
            .up_temp                    = 79,
            .down_temp                  = 76,
            .big_cluster_dvfs_opp       = 2,
            .little_cluster_dvfs_opp    = 2,
        }
    }
};

static const pbus_boot_metadata_t vim_thermal_metadata[] = {
    {
        .type       = DEVICE_METADATA_DRIVER_DATA,
        .extra      = 0,
        .data       = &aml_vim2_config,
        .len        = sizeof(aml_vim2_config),
    }
};

static const pbus_dev_t thermal_dev = {
    .name = "vim-thermal",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_THERMAL,
    .gpios = fanctl_gpios,
    .gpio_count = countof(fanctl_gpios),
    .boot_metadata = vim_thermal_metadata,
    .boot_metadata_count = countof(vim_thermal_metadata),
};

zx_status_t vim2_thermal_init(vim_bus_t* bus) {

    zx_status_t status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_SCPI);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_thermal_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &thermal_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim2_thermal_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}
