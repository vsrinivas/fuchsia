// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fidl/fuchsia.hardware.thermal/cpp/wire.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/sherlock-cpu-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace {
namespace fpbus = fuchsia_hardware_platform_bus;

const std::vector<fpbus::Mmio> cpu_mmios{
    {{
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    }},
};

constexpr amlogic_cpu::legacy_cluster_size_t cluster_sizes[] = {
    {.pd_id =
         static_cast<uint32_t>(fuchsia_hardware_thermal::wire::PowerDomain::kBigClusterPowerDomain),
     .core_count = 4},
    {.pd_id = static_cast<uint32_t>(
         fuchsia_hardware_thermal::wire::PowerDomain::kLittleClusterPowerDomain),
     .core_count = 2},
};

const std::vector<fpbus::Metadata> cpu_metadata{
    {{
        .type = DEVICE_METADATA_CLUSTER_SIZE_LEGACY,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&cluster_sizes),
            reinterpret_cast<const uint8_t*>(&cluster_sizes) + sizeof(cluster_sizes)),
    }},
};

const fpbus::Node cpu_dev = []() {
  fpbus::Node result = {};
  result.name() = "aml-cpu";
  result.vid() = PDEV_VID_GOOGLE;
  result.pid() = PDEV_PID_SHERLOCK;
  result.did() = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.metadata() = cpu_metadata;
  result.mmio() = cpu_mmios;
  return result;
}();

}  // namespace

namespace sherlock {

zx_status_t Sherlock::SherlockCpuInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SHER');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, cpu_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_cpu_fragments,
                                               std::size(aml_cpu_fragments)),
      "thermal");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite SherlockCpu(cpu_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite SherlockCpu(cpu_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace sherlock
