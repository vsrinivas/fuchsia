// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a1/a1-hw.h>

#include "clover.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace clover {
namespace fpbus = fuchsia_hardware_platform_bus;
namespace fht = fuchsia_hardware_thermal;

static const std::vector<fpbus::Mmio> thermal_pll_mmios{
    {{
        .base = A1_TEMP_SENSOR_PLL_BASE,
        .length = A1_TEMP_SENSOR_PLL_LENGTH,
    }},
    {{
        // we read the trim info from the secure register
        // and save it in the sticky register
        .base = A1_TEMP_SENSOR_PLL_TRIM,
        .length = A1_TEMP_SENSOR_PLL_TRIM_LENGTH,
    }},
    {{
        .base = A1_CLK_BASE,
        .length = A1_CLK_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> thermal_pll_irqs{
    {{
        .irq = A1_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

constexpr fht::wire::ThermalTemperatureInfo TripPoint(float temp_c, uint16_t cpu_opp_big,
                                                      uint16_t cpu_opp_little, uint16_t gpu_opp) {
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

static constexpr auto thermal_pll_config = fht::wire::ThermalDeviceInfo{
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 0,
    .big_little = true,
    .critical_temp_celsius = 101.0f,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {},
};

static const std::vector<fpbus::Metadata> thermal_pll_metadata{
    {{
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&thermal_pll_config),
            reinterpret_cast<const uint8_t*>(&thermal_pll_config) + sizeof(thermal_pll_config)),
    }},
};

static const fpbus::Node thermal_pll_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-thermal-pll";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A1;
  dev.did() = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio() = thermal_pll_mmios;
  dev.irq() = thermal_pll_irqs;
  dev.metadata() = thermal_pll_metadata;
  return dev;
}();

zx_status_t Clover::ThermalInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('THER');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermal_pll_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "NodeAdd Thermal(thermal_pll_dev) request failed: %s",
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "NodeAdd Thermal(thermal_pll_dev) failed: %s",
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace clover
