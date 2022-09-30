// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> thermal_mmios_pll{
    {{
        .base = T931_TEMP_SENSOR_PLL_BASE,
        .length = T931_TEMP_SENSOR_PLL_LENGTH,
    }},
    {{
        .base = T931_TEMP_SENSOR_PLL_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    }},
    {{
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    }},
};

static const std::vector<fpbus::Mmio> thermal_mmios_ddr{
    {{
        .base = T931_TEMP_SENSOR_DDR_BASE,
        .length = T931_TEMP_SENSOR_DDR_LENGTH,
    }},
    {{
        .base = T931_TEMP_SENSOR_DDR_TRIM,
        .length = T931_TEMP_SENSOR_TRIM_LENGTH,
    }},
    {{
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> thermal_irqs_pll{
    {{
        .irq = T931_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const std::vector<fpbus::Irq> thermal_irqs_ddr{
    {{
        .irq = T931_TS_DDR_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
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

static const std::vector<fpbus::Metadata> thermal_metadata{
    {{
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&aml_luis_config),
            reinterpret_cast<const uint8_t*>(&aml_luis_config) + sizeof(aml_luis_config)),
    }},
};

static const fpbus::Node thermal_dev_pll = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-thermal-pll";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_T931;
  dev.did() = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio() = thermal_mmios_pll;
  dev.irq() = thermal_irqs_pll;
  dev.metadata() = thermal_metadata;
  return dev;
}();

static const fpbus::Node thermal_dev_ddr = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-thermal-ddr";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_T931;
  dev.did() = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio() = thermal_mmios_ddr;
  dev.irq() = thermal_irqs_ddr;
  dev.metadata() = thermal_metadata;
  return dev;
}();

zx_status_t Sherlock::LuisThermalInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('LUIS');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermal_dev_pll));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd LuisThermal(thermal_dev_pll) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd LuisThermal(thermal_dev_pll) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermal_dev_ddr));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd LuisThermal(thermal_dev_ddr) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd LuisThermal(thermal_dev_ddr) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
