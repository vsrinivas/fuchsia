// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/thermal/c/fidl.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-clk.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"
#include "src/devices/board/drivers/mt8167s_ref/mt8167_bind.h"

namespace {

constexpr pbus_mmio_t thermal_mmios[] = {
    {.base = MT8167_THERMAL_BASE, .length = MT8167_THERMAL_SIZE},
    {.base = MT8167_FUSE_BASE, .length = MT8167_FUSE_SIZE},
    {.base = MT8167_AP_MIXED_SYS_BASE, .length = MT8167_AP_MIXED_SYS_SIZE},
    {.base = MT8167_PMIC_WRAP_BASE, .length = MT8167_PMIC_WRAP_SIZE},
    {.base = MT8167_INFRACFG_BASE, .length = MT8167_INFRACFG_SIZE}};

constexpr pbus_irq_t thermal_irqs[] = {
    {.irq = MT8167_IRQ_PTP_THERM, .mode = ZX_INTERRUPT_MODE_EDGE_HIGH}};

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c, uint16_t opp) {
  constexpr float kHysteresis = 2.0f;

  return {.up_temp_celsius = temp_c + kHysteresis,
          .down_temp_celsius = temp_c - kHysteresis,
          .fan_level = 0,
          .big_cluster_dvfs_opp = opp,
          .little_cluster_dvfs_opp = 0,
          .gpu_clk_freq_source = 0};
}

constexpr fuchsia_hardware_thermal_ThermalDeviceInfo
    thermal_dev_info =
        {.active_cooling = false,
         .passive_cooling = true,
         .gpu_throttling = true,
         .num_trip_points = 5,
         .big_little = false,
         .critical_temp_celsius = 120.0f,
         .trip_point_info =
             {
                 TripPoint(55.0f, 4),
                 TripPoint(65.0f, 3),
                 TripPoint(75.0f, 2),
                 TripPoint(85.0f, 1),
                 TripPoint(95.0f, 0),
             },
         .opps = {[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                      {// See section 3.6 (MTCMOS Domains) of the functional specification document.
                       // Use 1.3V because the GPU uses this voltage as well and needs that to clock
                       // up to 600MHz.
                       // TODO(fxbug.dev/35052): - Coordinate voltage between GPU and CPU.
                       .opp =
                           {
                               [0] = {.freq_hz = 598000000, .volt_uv = 1'300'000},
                               [1] = {.freq_hz = 747500000, .volt_uv = 1'300'000},
                               [2] = {.freq_hz = 1040000000, .volt_uv = 1'300'000},
                               [3] = {.freq_hz = 1196000000, .volt_uv = 1'300'000},
                               [4] = {.freq_hz = 1300000000, .volt_uv = 1'300'000},
                           },
                       .latency = 0,
                       .count = 5},
                  [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] = {
                      .opp = {}, .latency = 0, .count = 0}}};

const pbus_metadata_t thermal_metadata[] = {
    {.type = DEVICE_METADATA_THERMAL_CONFIG,
     .data_buffer = reinterpret_cast<const uint8_t*>(&thermal_dev_info),
     .data_size = sizeof(thermal_dev_info)},
};

const pbus_dev_t thermal_dev = []() {
  pbus_dev_t thermal_dev = {};
  thermal_dev.name = "thermal";
  thermal_dev.vid = PDEV_VID_MEDIATEK;
  thermal_dev.did = PDEV_DID_MEDIATEK_THERMAL;
  thermal_dev.mmio_list = thermal_mmios;
  thermal_dev.mmio_count = countof(thermal_mmios);
  thermal_dev.metadata_list = thermal_metadata;
  thermal_dev.metadata_count = countof(thermal_metadata);
  thermal_dev.irq_list = thermal_irqs;
  thermal_dev.irq_count = countof(thermal_irqs);
  return thermal_dev;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
static const zx_bind_inst_t clk1_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, board_mt8167::kClkThem),
};
static const zx_bind_inst_t clk2_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, board_mt8167::kClkAuxAdc),
};
static const zx_bind_inst_t clk3_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, board_mt8167::kClkPmicwrapAp),
};
static const zx_bind_inst_t clk4_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, board_mt8167::kClkPmicwrap26m),
};
static const device_fragment_part_t clk1_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk1_match), clk1_match},
};
static const device_fragment_part_t clk2_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk2_match), clk2_match},
};
static const device_fragment_part_t clk3_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk3_match), clk3_match},
};
static const device_fragment_part_t clk4_fragment[] = {
    {countof(root_match), root_match},
    {countof(clk4_match), clk4_match},
};
static const device_fragment_t fragments[] = {
    {"clock-1", countof(clk1_fragment), clk1_fragment},
    {"clock-2", countof(clk2_fragment), clk2_fragment},
    {"clock-3", countof(clk3_fragment), clk3_fragment},
    {"clock-4", countof(clk4_fragment), clk4_fragment},
};

}  // namespace

namespace board_mt8167 {

zx_status_t Mt8167::ThermalInit() {
  auto status = pbus_.CompositeDeviceAdd(&thermal_dev, reinterpret_cast<uint64_t>(fragments),
                                         countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd thermal failed: %d", __FUNCTION__, status);
  }

  return status;
}

}  // namespace board_mt8167
