// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-clk.h>
#include <soc/vs680/vs680-hw.h>
#include <soc/vs680/vs680-power.h>

#include "luis.h"

namespace board_luis {

zx_status_t Luis::ThermalInit() {
  constexpr pbus_mmio_t thermal_mmios[] = {
      {
          .base = vs680::kCpuWrpBase,
          .length = vs680::kCpuWrpSize,
      },
  };

  constexpr pbus_irq_t thermal_irqs[] = {
      {
          .irq = vs680::kTempSensorIrq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  constexpr zx_bind_inst_t cpu_clock_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
      BI_MATCH_IF(EQ, BIND_CLOCK_ID, vs680::kCpuPll),
  };

  constexpr zx_bind_inst_t cpu_power_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
      BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, vs680::kPowerDomainVCpu),
  };

  const device_fragment_part_t cpu_clock_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(cpu_clock_match), cpu_clock_match},
  };

  const device_fragment_part_t cpu_power_fragment[] = {
      {fbl::count_of(root_match), root_match},
      {fbl::count_of(cpu_power_match), cpu_power_match},
  };

  const device_fragment_t thermal_fragments[] = {
      {fbl::count_of(cpu_clock_fragment), cpu_clock_fragment},
      {fbl::count_of(cpu_power_fragment), cpu_power_fragment},
  };

  pbus_dev_t thermal_dev = {};
  thermal_dev.name = "vs680-thermal";
  thermal_dev.vid = PDEV_VID_SYNAPTICS;
  thermal_dev.did = PDEV_DID_VS680_THERMAL;
  thermal_dev.mmio_list = thermal_mmios;
  thermal_dev.mmio_count = fbl::count_of(thermal_mmios);
  thermal_dev.irq_list = thermal_irqs;
  thermal_dev.irq_count = fbl::count_of(thermal_irqs);

  zx_status_t status = pbus_.CompositeDeviceAdd(&thermal_dev, thermal_fragments,
                                                fbl::count_of(thermal_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_luis
