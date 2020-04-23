// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "astro.h"

namespace {

constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_AMLOGIC;
  result.pid = PDEV_PID_AMLOGIC_S905D2;
  result.did = PDEV_DID_AMLOGIC_CPU;
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

namespace astro {

zx_status_t Astro::CpuInit() {
  zx_status_t result = pbus_.CompositeDeviceAdd(&cpu_dev, fragments, countof(fragments), 1);

  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d", __func__, result);
  }

  return result;
}

}  // namespace astro
