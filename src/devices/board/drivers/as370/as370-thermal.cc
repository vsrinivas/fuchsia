// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-power.h>
#include <soc/as370/as370-thermal.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace board_as370 {
namespace fpbus = fuchsia_hardware_platform_bus;

using fuchsia_hardware_thermal::wire::OperatingPoint;
using fuchsia_hardware_thermal::wire::OperatingPointEntry;
using fuchsia_hardware_thermal::wire::PowerDomain;
using fuchsia_hardware_thermal::wire::ThermalDeviceInfo;

zx_status_t As370::ThermalInit() {
  static const std::vector<fpbus::Mmio> thermal_mmios{
      {{
          .base = as370::kThermalBase,
          .length = as370::kThermalSize,
      }},
  };

  constexpr ThermalDeviceInfo kThermalDeviceInfo = {
      .active_cooling = false,
      .passive_cooling = true,
      .gpu_throttling = false,
      .num_trip_points = 0,
      .big_little = false,
      .critical_temp_celsius = 0.0f,
      .trip_point_info = {},
      .opps =
          fidl::Array<OperatingPoint, 2>{
              OperatingPoint{
                  .opp =
                      fidl::Array<OperatingPointEntry, 16>{
                          // clang-format off
                          OperatingPointEntry{.freq_hz =   400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz =   800'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'200'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'500'000'000, .volt_uv = 900'000},
                          OperatingPointEntry{.freq_hz = 1'800'000'000, .volt_uv = 900'000},
                          // clang-format on
                      },
                  .latency = 0,
                  .count = 6,
              },
              {
                  .opp = {},
                  .latency = 0,
                  .count = 0,
              },
          },
  };

  std::vector<fpbus::Metadata> thermal_metadata{
      {{
          .type = DEVICE_METADATA_THERMAL_CONFIG,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&kThermalDeviceInfo),
              reinterpret_cast<const uint8_t*>(&kThermalDeviceInfo) + sizeof(kThermalDeviceInfo)),
      }},
  };

  static constexpr zx_bind_inst_t cpu_clock_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
      BI_MATCH_IF(EQ, BIND_CLOCK_ID, as370::kClkCpu),
  };
  static const device_fragment_part_t cpu_clock_fragment[] = {
      {std::size(cpu_clock_match), cpu_clock_match},
  };

  static constexpr zx_bind_inst_t cpu_power_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
      BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, kBuckSoC),
  };
  static const device_fragment_part_t cpu_power_fragment[] = {
      {std::size(cpu_power_match), cpu_power_match},
  };

  static const device_fragment_t fragments[] = {
      {"clock", std::size(cpu_clock_fragment), cpu_clock_fragment},
      {"power", std::size(cpu_power_fragment), cpu_power_fragment},
  };

  fpbus::Node thermal_dev;
  thermal_dev.name() = "thermal";
  thermal_dev.vid() = PDEV_VID_SYNAPTICS;
  thermal_dev.did() = PDEV_DID_AS370_THERMAL;
  thermal_dev.mmio() = thermal_mmios;
  thermal_dev.metadata() = thermal_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('THER');
  auto result = pbus_.buffer(arena)->AddCompositeImplicitPbusFragment(
      fidl::ToWire(fidl_arena, thermal_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, fragments, std::size(fragments)), {});
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Thermal(thermal_dev) request failed: %s",
           __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddCompositeImplicitPbusFragment Thermal(thermal_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace board_as370
