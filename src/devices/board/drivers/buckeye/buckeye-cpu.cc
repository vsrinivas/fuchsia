// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/syscalls/smc.h>

#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-a5/a5-power.h>
#include <soc/aml-common/aml-cpu-metadata.h>

#include "buckeye.h"
#include "src/devices/board/drivers/buckeye/buckeye-cpu-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace {

namespace fpbus = fuchsia_hardware_platform_bus;

static constexpr amlogic_cpu::PerfDomainId kPdArmA55 = 1;

static const std::vector<fpbus::Mmio> cpu_mmios{
    {{
        .base = A5_SYS_CTRL_BASE,
        .length = A5_SYS_CTRL_LENGTH,
    }},
};

static constexpr amlogic_cpu::operating_point_t operating_0_points[] = {
    {.freq_hz = 100'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 250'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 500'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 667'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'000'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'200'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'404'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'500'000'000, .volt_uv = 799'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'608'000'000, .volt_uv = 829'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'704'000'000, .volt_uv = 869'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'800'000'000, .volt_uv = 909'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'920'000'000, .volt_uv = 969'000, .pd_id = kPdArmA55},
    {.freq_hz = 2'016'000'000, .volt_uv = 1'009'000, .pd_id = kPdArmA55},
};

constexpr amlogic_cpu::operating_point_t operating_1_points[] = {
    {.freq_hz = 100'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 250'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 500'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 667'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'000'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'200'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'404'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'500'000'000, .volt_uv = 799'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'608'000'000, .volt_uv = 829'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'704'000'000, .volt_uv = 869'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'800'000'000, .volt_uv = 909'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'920'000'000, .volt_uv = 969'000, .pd_id = kPdArmA55},
    {.freq_hz = 2'016'000'000, .volt_uv = 1'009'000, .pd_id = kPdArmA55},
};

constexpr amlogic_cpu::operating_point_t operating_2_points[] = {
    {.freq_hz = 100'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 250'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 500'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 667'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'000'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'200'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'404'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'500'000'000, .volt_uv = 789'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'608'000'000, .volt_uv = 799'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'704'000'000, .volt_uv = 829'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'800'000'000, .volt_uv = 859'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'920'000'000, .volt_uv = 919'000, .pd_id = kPdArmA55},
    {.freq_hz = 2'016'000'000, .volt_uv = 949'000, .pd_id = kPdArmA55},
};

constexpr amlogic_cpu::operating_point_t operating_3_points[] = {
    {.freq_hz = 100'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 250'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 500'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 667'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'000'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'200'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'404'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'500'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'608'000'000, .volt_uv = 769'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'704'000'000, .volt_uv = 799'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'800'000'000, .volt_uv = 829'000, .pd_id = kPdArmA55},
    {.freq_hz = 1'920'000'000, .volt_uv = 889'000, .pd_id = kPdArmA55},
    {.freq_hz = 2'016'000'000, .volt_uv = 929'000, .pd_id = kPdArmA55},
};

static constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA55, .core_count = 4, .relative_performance = 255, .name = "a5-arm-a55"},
};

static const std::vector<fpbus::Metadata> cpu_metadata{
    {{
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&operating_0_points),
            reinterpret_cast<const uint8_t*>(&operating_0_points) + sizeof(operating_0_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_OP_1_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&operating_1_points),
            reinterpret_cast<const uint8_t*>(&operating_1_points) + sizeof(operating_1_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_OP_2_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&operating_2_points),
            reinterpret_cast<const uint8_t*>(&operating_2_points) + sizeof(operating_2_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_OP_3_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&operating_3_points),
            reinterpret_cast<const uint8_t*>(&operating_3_points) + sizeof(operating_3_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&performance_domains),
            reinterpret_cast<const uint8_t*>(&performance_domains) + sizeof(performance_domains)),
    }},
};

static const std::vector<fpbus::Smc> cpu_smcs{
    {{
        .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
        .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        .exclusive = false,
    }},
};

static const fpbus::Node cpu_dev = []() {
  fpbus::Node result = {};
  result.name() = "aml-cpu";
  result.vid() = PDEV_VID_AMLOGIC;
  result.pid() = PDEV_PID_AMLOGIC_A5;
  result.did() = PDEV_DID_AMLOGIC_CPU;
  result.metadata() = cpu_metadata;
  result.mmio() = cpu_mmios;
  result.smc() = cpu_smcs;

  return result;
}();

}  // namespace

namespace buckeye {

zx_status_t Buckeye::CpuInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('CPU_');
  auto composite_result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, cpu_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, aml_cpu_fragments,
                                               std::size(aml_cpu_fragments)),
      fidl::StringView::FromExternal("power-01"));

  if (!composite_result.ok()) {
    zxlogf(ERROR, "%s: AddComposite request failed: %s", __func__,
           composite_result.FormatDescription().data());
    return composite_result.status();
  }
  if (composite_result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite failed: %s", __func__,
           zx_status_get_string(composite_result->error_value()));
    return composite_result->error_value();
  }

  return ZX_OK;
}

}  // namespace buckeye
