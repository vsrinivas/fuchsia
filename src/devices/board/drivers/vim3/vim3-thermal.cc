// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/camera.h>
#include <soc/aml-a311d/a311d-gpio.h>
#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-a311d/a311d-pwm.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-meson/g12b-clk.h>

#include "vim3.h"

namespace vim3 {
namespace fpbus = fuchsia_hardware_platform_bus;

namespace {

const std::vector<fpbus::Mmio> thermal_mmios_pll{
    {{
        .base = A311D_TEMP_SENSOR_PLL_BASE,
        .length = A311D_TEMP_SENSOR_PLL_LENGTH,
    }},
    {{
        .base = A311D_TEMP_SENSOR_PLL_TRIM,
        .length = A311D_TEMP_SENSOR_TRIM_LENGTH,
    }},
    {{
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    }},
};

const std::vector<fpbus::Mmio> thermal_mmios_ddr{
    {{
        .base = A311D_TEMP_SENSOR_DDR_BASE,
        .length = A311D_TEMP_SENSOR_DDR_LENGTH,
    }},
    {{
        .base = A311D_TEMP_SENSOR_DDR_TRIM,
        .length = A311D_TEMP_SENSOR_TRIM_LENGTH,
    }},
    {{
        .base = A311D_HIU_BASE,
        .length = A311D_HIU_LENGTH,
    }},
};

const std::vector<fpbus::Irq> thermal_irqs_pll{
    {{
        .irq = A311D_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

const std::vector<fpbus::Irq> thermal_irqs_ddr{
    {{
        .irq = A311D_TS_DDR_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

constexpr fuchsia_hardware_thermal::wire::ThermalTemperatureInfo TripPoint(float temp_c,
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

constexpr fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config_pll = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 0,
    .big_little = true,
    .critical_temp_celsius = 101.0f,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {},
};

constexpr fuchsia_hardware_thermal::wire::ThermalDeviceInfo thermal_config_ddr = {
    .active_cooling = false,
    .passive_cooling = false,
    .gpu_throttling = false,
    .num_trip_points = 0,
    .big_little = false,
    .critical_temp_celsius = 110.0,
    .trip_point_info = {TripPoint(-273.15f, 0, 0, 0)},  // Unused
    .opps = {},
};

const std::vector<fpbus::Metadata> thermal_metadata_pll{
    {{
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&thermal_config_pll),
            reinterpret_cast<const uint8_t*>(&thermal_config_pll) + sizeof(thermal_config_pll)),
    }},
};

const std::vector<fpbus::Metadata> thermal_metadata_ddr{
    {{
        .type = DEVICE_METADATA_THERMAL_CONFIG,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&thermal_config_ddr),
            reinterpret_cast<const uint8_t*>(&thermal_config_ddr) + sizeof(thermal_config_ddr)),
    }},
};

const fpbus::Node thermal_dev_pll = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-thermal-pll";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_THERMAL_PLL;
  dev.mmio() = thermal_mmios_pll;
  dev.irq() = thermal_irqs_pll;
  dev.metadata() = thermal_metadata_pll;
  return dev;
}();

const fpbus::Node thermal_dev_ddr = []() {
  fpbus::Node dev = {};
  dev.name() = "aml-thermal-ddr";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_AMLOGIC_A311D;
  dev.did() = PDEV_DID_AMLOGIC_THERMAL_DDR;
  dev.mmio() = thermal_mmios_ddr;
  dev.irq() = thermal_irqs_ddr;
  dev.metadata() = thermal_metadata_ddr;
  return dev;
}();

}  // namespace

zx_status_t Vim3::ThermalInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('THER');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermal_dev_pll));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Thermal(thermal_dev_pll) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Thermal(thermal_dev_pll) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, thermal_dev_ddr));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Thermal(thermal_dev_ddr) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Thermal(thermal_dev_ddr) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace vim3
