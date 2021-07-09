// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace {

constexpr pbus_mmio_t cpu_mmios[]{
    {
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
};

constexpr amlogic_cpu::legacy_cluster_size_t cluster_sizes[] = {
    {.pd_id = fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, .core_count = 4},
    {.pd_id = fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, .core_count = 2},
};

static const pbus_metadata_t cpu_metadata[] = {
    {
        .type = DEVICE_METADATA_CLUSTER_SIZE_LEGACY,
        .data_buffer = reinterpret_cast<const uint8_t*>(cluster_sizes),
        .data_size = sizeof(cluster_sizes),
    },
};

constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_GOOGLE;
  result.pid = PDEV_PID_SHERLOCK;
  result.did = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.metadata_list = cpu_metadata;
  result.metadata_count = countof(cpu_metadata);
  result.mmio_list = cpu_mmios;
  result.mmio_count = countof(cpu_mmios);
  return result;
}();

// The CPU device must bind to a legacy thermal driver to which DVFS commands are forwarded.
// We need to specify the PLL sensor to ensure the correct bind, as there is a non-legacy thermal
// device controlling the DDR sensor.
constexpr zx_bind_inst_t thermal_match[] = {
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_THERMAL_PLL),
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_THERMAL),
};

constexpr device_fragment_part_t thermal_fragment[] = {
    {countof(thermal_match), thermal_match},
};

constexpr device_fragment_t fragments[] = {
    {"thermal", countof(thermal_fragment), thermal_fragment},
};

}  // namespace

namespace sherlock {

zx_status_t Sherlock::SherlockCpuInit() {
  zx_status_t result = pbus_.CompositeDeviceAdd(&cpu_dev, reinterpret_cast<uint64_t>(fragments),
                                                countof(fragments), "thermal");

  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d\n", __func__, result);
  }

  return result;
}

}  // namespace sherlock
