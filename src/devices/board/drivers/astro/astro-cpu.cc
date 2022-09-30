// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-power.h>

#include "astro-gpios.h"
#include "astro.h"
#include "src/devices/board/drivers/astro/astro-cpu-bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace {
namespace fpbus = fuchsia_hardware_platform_bus;

constexpr amlogic_cpu::PerfDomainId kPdArmA53 = 1;

const std::vector<fpbus::Mmio> cpu_mmios{
    {{
        // AOBUS
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    }},
};

constexpr amlogic_cpu::operating_point_t operating_points[] = {
    {.freq_hz = 100'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 250'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 500'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 667'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'000'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'200'000'000, .volt_uv = 731'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'398'000'000, .volt_uv = 761'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'512'000'000, .volt_uv = 791'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'608'000'000, .volt_uv = 831'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'704'000'000, .volt_uv = 861'000, .pd_id = kPdArmA53},
    {.freq_hz = 1'896'000'000, .volt_uv = 1'022'000, .pd_id = kPdArmA53},
};

constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA53, .core_count = 4, .relative_performance = 255, .name = "s905d2-arm-a53"},
};

static const std::vector<fpbus::Metadata> cpu_metadata{
    {{
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&operating_points),
            reinterpret_cast<const uint8_t*>(&operating_points) + sizeof(operating_points)),
    }},
    {{
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&performance_domains),
            reinterpret_cast<const uint8_t*>(&performance_domains) + sizeof(performance_domains)),
    }},
};

static const fpbus::Node cpu_dev = []() {
  fpbus::Node result = {};
  result.name() = "aml-cpu";
  result.vid() = PDEV_VID_GOOGLE;
  result.pid() = PDEV_PID_ASTRO;
  result.did() = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.metadata() = cpu_metadata;
  result.mmio() = cpu_mmios;
  return result;
}();

}  // namespace

namespace astro {

zx_status_t Astro::CpuInit() {
  zx_status_t result;
  result = gpio_impl_.ConfigOut(S905D2_PWM_D_PIN, 0);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: ConfigOut failed: %d", __func__, result);
    return result;
  }

  // Configure the GPIO to be Output & set it to alternate
  // function 3 which puts in PWM_D mode.
  result = gpio_impl_.SetAltFunction(S905D2_PWM_D_PIN, S905D2_PWM_D_FN);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: SetAltFunction failed: %d", __func__, result);
    return result;
  }

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

}  // namespace astro
