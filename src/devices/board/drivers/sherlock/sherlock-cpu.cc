// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
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

constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_GOOGLE;
  result.pid = PDEV_PID_SHERLOCK;
  result.did = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.mmio_list = cpu_mmios;
  result.mmio_count = countof(cpu_mmios);
  return result;
}();

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

constexpr zx_bind_inst_t thermal_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_THERMAL),
};

constexpr device_fragment_part_t thermal_fragment[] = {
    {countof(root_match), root_match},
    {countof(thermal_match), thermal_match},
};

constexpr device_fragment_t fragments[] = {
    {countof(thermal_fragment), thermal_fragment},
};

}  // namespace

namespace sherlock {

zx_status_t Sherlock::SherlockCpuInit() {
  zx_status_t result = pbus_.CompositeDeviceAdd(&cpu_dev, fragments, countof(fragments), 1);

  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d\n", __func__, result);
  }

  return result;
}

}  // namespace sherlock
