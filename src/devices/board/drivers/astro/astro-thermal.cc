// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

namespace astro {

static const pbus_mmio_t thermal_mmios_pll[] = {
    {
        .base = S905D2_TEMP_SENSOR_PLL_BASE,
        .length = S905D2_TEMP_SENSOR_PLL_LENGTH,
    },
    {
        .base = S905D2_TEMP_SENSOR_PLL_TRIM,
        .length = S905D2_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_mmio_t thermal_mmios_ddr[] = {
    {
        .base = S905D2_TEMP_SENSOR_DDR_BASE,
        .length = S905D2_TEMP_SENSOR_DDR_LENGTH,
    },
    {
        .base = S905D2_TEMP_SENSOR_DDR_TRIM,
        .length = S905D2_TEMP_SENSOR_TRIM_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
};

static const pbus_irq_t thermal_irqs_pll[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static const pbus_irq_t thermal_irqs_ddr[] = {
    {
        .irq = S905D2_TS_DDR_IRQ,
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

fuchsia_hardware_thermal_ThermalDeviceInfo astro_config = {
    .active_cooling = false,
    .passive_cooling = false,
    .gpu_throttling = false,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp_celsius = 0.0,
    .trip_point_info =
        {
            TripPoint(-273.15f, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
        },
    .opps = {}};

static const pbus_metadata_t thermal_metadata[] = {
    {
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data_buffer = &astro_config,
        .data_size = sizeof(astro_config),
    },
};

static pbus_dev_t thermal_dev_pll = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-pll";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio_list = thermal_mmios_pll;
  dev.mmio_count = countof(thermal_mmios_pll);
  dev.irq_list = thermal_irqs_pll;
  dev.irq_count = countof(thermal_irqs_pll);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  return dev;
}();

static pbus_dev_t thermal_dev_ddr = []() {
  pbus_dev_t dev = {};
  dev.name = "aml-thermal-ddr";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_AMLOGIC_S905D2;
  dev.did = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio_list = thermal_mmios_ddr;
  dev.mmio_count = countof(thermal_mmios_ddr);
  dev.irq_list = thermal_irqs_ddr;
  dev.irq_count = countof(thermal_irqs_ddr);
  dev.metadata_list = thermal_metadata;
  dev.metadata_count = countof(thermal_metadata);
  return dev;
}();

zx_status_t Astro::ThermalInit() {
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

}  // namespace astro
