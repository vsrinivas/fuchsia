// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D2_TEMP_SENSOR_BASE,
        .length = S905D2_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D2_TEMP_SENSOR_TRIM,
        .length = S905D2_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c, uint16_t cpu_opp,
                                                                    uint16_t gpu_opp) {
  constexpr float kHysteresis = 2.0f;

  return {
      .up_temp_celsius = temp_c + kHysteresis,
      .down_temp_celsius = temp_c - kHysteresis,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp,
      .little_cluster_dvfs_opp = 0,
      .gpu_clk_freq_source = gpu_opp,
  };
}

/*
 * GPU_CLK_FREQUENCY_SOURCE -
 * 0 - 285.7 MHz
 * 1 - 400 MHz
 * 2 - 500 MHz
 * 3 - 666 MHz
 * 4 - 800 MHz
 * 5 - 846 MHz
 */

// NOTE: This is a very trivial policy, no data backing it up
// As we do more testing this policy can evolve.
static fuchsia_hardware_thermal_ThermalDeviceInfo astro_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 7,
    .big_little = false,
    .critical_temp_celsius = 102.0f,
    .trip_point_info =
        {
            // The first trip point entry is the default state of the machine
            // and the driver does not use the specified temperature/hysterisis
            // to set any interrupt trip points.
            TripPoint(0.0f, 10, 5), TripPoint(75.0f, 9, 4), TripPoint(80.0f, 8, 3),
            TripPoint(85.0f, 7, 3), TripPoint(90.0f, 6, 2), TripPoint(95.0f, 5, 1),
            TripPoint(100.0f, 4, 0),
            TripPoint(-273.15f, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
        },
    .opps =
        {
            // Considering this as LITTLE one since in BIG-LITTLE arch for same
            // thermal driver, these settings apply to the LITTLE cluster.
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                {
                    .opp = {{0, 0}},
                    .latency = 0,
                    .count = 0,
                },
        },
};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = &astro_config,
        .data_size = sizeof(astro_config),
    },
};

static pbus_dev_t thermal_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_THERMAL;
  dev.mmio_list = thermal_mmios;
  dev.mmio_count = countof(thermal_mmios);
  dev.irq_list = thermal_irqs;
  dev.irq_count = countof(thermal_irqs);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  return dev;
}();

zx_status_t Astro::ThermalInit() {
  zx_status_t status = pbus_.DeviceAdd(&thermal_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }
  return ZX_OK;
}

}  // namespace astro
