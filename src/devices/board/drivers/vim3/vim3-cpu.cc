// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a311d/a311d-hw.h>
#include <soc/aml-a311d/a311d-power.h>
#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-meson/g12b-clk.h>

#include "src/devices/board/drivers/vim3/vim3-cpu-bind.h"
#include "src/devices/board/drivers/vim3/vim3.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace {
namespace fpbus = fuchsia_hardware_platform_bus;

constexpr amlogic_cpu::PerfDomainId kPdArmA53 = 1;
constexpr amlogic_cpu::PerfDomainId kPdArmA73 = 2;

const std::vector<fpbus::Mmio> cpu_mmios{
    {{
        // AOBUS
        .base = A311D_AOBUS_BASE,
        .length = A311D_AOBUS_LENGTH,
    }},
};

constexpr amlogic_cpu::operating_point_t operating_points[] = {
    // Little Cluster DVFS Table
    {.freq_hz = 500'000'000, .volt_uv = 730'000, .pd_id = kPdArmA53},
    {.freq_hz = 667'000'000, .volt_uv = 730'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'000'000'000, .volt_uv = 760'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'200'000'000, .volt_uv = 780'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'398'000'000, .volt_uv = 810'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'512'000'000, .volt_uv = 860'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'608'000'000, .volt_uv = 900'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'704'000'000, .volt_uv = 950'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'800'000'000, .volt_uv = 1'020'000, .pd_id = kPdArmA53},

    // Big Cluster DVFS Table
    {.freq_hz = 500'000'000, .volt_uv = 730'000, .pd_id = kPdArmA73},
    {.freq_hz = 667'000'000, .volt_uv = 730'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'000'000'000, .volt_uv = 730'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'200'000'000, .volt_uv = 750'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'398'000'000, .volt_uv = 770'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'512'000'000, .volt_uv = 770'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'608'000'000, .volt_uv = 780'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'704'000'000, .volt_uv = 790'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'800'000'000, .volt_uv = 830'000, .pd_id = kPdArmA73},
    {.freq_hz = 1'908'000'000, .volt_uv = 860'000, .pd_id = kPdArmA73},
    {.freq_hz = 2'016'000'000, .volt_uv = 910'000, .pd_id = kPdArmA73},
    {.freq_hz = 2'100'000'000, .volt_uv = 960'000, .pd_id = kPdArmA73},
    {.freq_hz = 2'208'000'000, .volt_uv = 1'030'000, .pd_id = kPdArmA73},
};

constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA73, .core_count = 4, .name = "a311d-arm-a73"},
    {.id = kPdArmA53, .core_count = 2, .name = "a311d-arm-a53"},
};

const std::vector<fpbus::Metadata> cpu_metadata{
    {{
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(operating_points),
            reinterpret_cast<const uint8_t*>(operating_points) + sizeof(operating_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(performance_domains),
            reinterpret_cast<const uint8_t*>(performance_domains) + sizeof(performance_domains)),
    }},
};

const fpbus::Node cpu_dev = []() {
  fpbus::Node result = {};
  result.name() = "aml-cpu";
  result.vid() = PDEV_VID_AMLOGIC;
  result.pid() = PDEV_PID_AMLOGIC_A311D;
  result.did() = PDEV_DID_AMLOGIC_CPU;
  result.metadata() = cpu_metadata;
  result.mmio() = cpu_mmios;
  return result;
}();

}  // namespace

namespace vim3 {

zx_status_t Vim3::CpuInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CPU_');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, cpu_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, vim3_cpu_fragments,
                                               std::size(vim3_cpu_fragments)),
      "power-01");
  if (!result.ok()) {
    zxlogf(ERROR, "Cpu(cpu_dev)Init: AddComposite Cpu(cpu_dev) request failed: %s",
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "Cpu(cpu_dev)Init: AddComposite Cpu(cpu_dev) failed: %s",
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace vim3
