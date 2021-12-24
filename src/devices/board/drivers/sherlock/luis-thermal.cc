// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t thermal_mmios_pll[] = {
    {
        .base = T931_TEMP_SENSOR_PLL_BASE,
        .length = T931_TEMP_SENSOR_PLL_LENGTH,
    },
    {
        .base = T931_TEMP_SENSOR_PLL_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

static const pbus_mmio_t thermal_mmios_ddr[] = {
    {
        .base = T931_TEMP_SENSOR_DDR_BASE,
        .length = T931_TEMP_SENSOR_DDR_LENGTH,
    },
    {
        .base = T931_TEMP_SENSOR_DDR_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs_pll[] = {
    {
        .irq = T931_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_irq_t thermal_irqs_ddr[] = {
    {
        .irq = T931_TS_DDR_IRQ,
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

fuchsia_hardware_thermal_ThermalDeviceInfo aml_luis_config = {
    .active_cooling = false,
    .passive_cooling = false,
    .gpu_throttling = false,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp_celsius = 0.0,
    .trip_point_info =
        {
            TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
        },
    .opps = {}};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = reinterpret_cast<uint8_t*>(&aml_luis_config),
        .data_size = sizeof(aml_luis_config),
    },
};

static pbus_dev_t thermal_dev_pll = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios_pll;
  dev.mmio_count = std::size(thermal_mmios_pll);
  dev.irq_list = thermal_irqs_pll;
  dev.irq_count = std::size(thermal_irqs_pll);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = std::size(thermal_metadata);
  return dev;
}();

static pbus_dev_t thermal_dev_ddr = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-ddr";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_T931;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio_list = thermal_mmios_ddr;
  dev.mmio_count = std::size(thermal_mmios_ddr);
  dev.irq_list = thermal_irqs_ddr;
  dev.irq_count = std::size(thermal_irqs_ddr);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = std::size(thermal_metadata);
  return dev;
}();

zx_status_t Sherlock::LuisThermalInit() {
  zx_status_t status;

  status = pbus_.DeviceAdd(&thermal_dev_pll);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  status = pbus_.DeviceAdd(&thermal_dev_ddr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
