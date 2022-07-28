// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-common/aml-thermal.h>

#include "av400.h"

namespace av400 {

static constexpr pbus_mmio_t thermal_pll_mmios[] = {
    {
        .base = A5_TEMP_SENSOR_PLL_BASE,
        .length = A5_TEMP_SENSOR_PLL_LENGTH,
    },
    {
        // we read the trim info from the secure register
        // and save it in the sticky register
        .base = A5_TEMP_SENSOR_PLL_TRIM,
        .length = A5_TEMP_SENSOR_PLL_TRIM_LENGTH,
    },
    {
        .base = A5_CLK_BASE,
        .length = A5_CLK_LENGTH,
    },
};

static constexpr pbus_irq_t thermal_pll_irqs[] = {
    {
        .irq = A5_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c,
                                                                    uint16_t cpu_opp_big,
                                                                    uint16_t cpu_opp_little,
                                                                    uint16_t gpu_opp) {
  constexpr float kHysteresis = 2.0f;

  return {
      .up_temp_celsius = temp_c + kHysteresis,
      .down_temp_celsius = temp_c - kHysteresis,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp_big,
      .little_cluster_dvfs_opp = cpu_opp_little,
      .gpu_clk_freq_source = gpu_opp,
  };
}

static constexpr fuchsia_hardware_thermal_ThermalDeviceInfo thermal_pll_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 0,
    .big_little = true,
    .critical_temp_celsius = 101.0f,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {},
};

const pbus_metadata_t thermal_pll_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = reinterpret_cast<const uint8_t*>(&thermal_pll_config),
        .data_size = sizeof(thermal_pll_config),
    },
};

static constexpr pbus_dev_t thermal_pll_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_A5;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_pll_mmios;
  dev.mmio_count = std::size(thermal_pll_mmios);
  dev.irq_list = thermal_pll_irqs;
  dev.irq_count = std::size(thermal_pll_irqs);
  dev.metadata_list = thermal_pll_metadata;
  dev.metadata_count = std::size(thermal_pll_metadata);
  return dev;
}();

zx_status_t Av400::ThermalInit() {
  auto status = pbus_.DeviceAdd(&thermal_pll_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "DeviceAdd failed: %s", zx_status_get_string(status));
    return status;
  }

  return status;
}

}  // namespace av400
