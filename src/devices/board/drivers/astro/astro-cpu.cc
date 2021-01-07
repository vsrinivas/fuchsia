// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-power.h>

#include "astro-gpios.h"
#include "astro.h"

namespace {

constexpr amlogic_cpu::PerfDomainId kPdArmA53 = 1;

constexpr pbus_mmio_t cpu_mmios[]{
    {
        // AOBUS
        .base = S905D2_AOBUS_BASE,
        .length = S905D2_AOBUS_LENGTH,
    },
};

constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};

constexpr zx_bind_inst_t power_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_POWER),
    BI_MATCH_IF(EQ, BIND_POWER_DOMAIN, static_cast<uint32_t>(S905d2PowerDomains::kArmCore)),
};

constexpr device_fragment_part_t power_dfp[] = {
    {countof(root_match), root_match},
    {countof(power_match), power_match},
};

constexpr zx_bind_inst_t clock_pll_div16_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12a_clk::CLK_SYS_PLL_DIV16),
};

constexpr device_fragment_part_t clock_pll_div16_dfp[] = {
    {countof(root_match), root_match},
    {countof(clock_pll_div16_match), clock_pll_div16_match},
};

constexpr zx_bind_inst_t clock_cpu_div16_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12a_clk::CLK_SYS_CPU_CLK_DIV16),
};

constexpr device_fragment_part_t clock_cpu_div16_dfp[] = {
    {countof(root_match), root_match},
    {countof(clock_cpu_div16_match), clock_cpu_div16_match},
};

constexpr zx_bind_inst_t clock_cpu_scaler_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
    BI_MATCH_IF(EQ, BIND_CLOCK_ID, g12a_clk::CLK_SYS_CPU_CLK),
};

constexpr device_fragment_part_t clock_cpu_scaler_dfp[] = {
    {countof(root_match), root_match},
    {countof(clock_cpu_scaler_match), clock_cpu_scaler_match},
};

constexpr device_fragment_t fragments[] = {
    {"power-01", countof(power_dfp), power_dfp},
    {"clock-pll-div16-01", countof(clock_pll_div16_dfp), clock_pll_div16_dfp},
    {"clock-cpu-div16-01", countof(clock_cpu_div16_dfp), clock_cpu_div16_dfp},
    {"clock-cpu-scaler-01", countof(clock_cpu_scaler_dfp), clock_cpu_scaler_dfp},
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
    {.freq_hz = 1'896'000'000, .volt_uv = 981'000, .pd_id = kPdArmA53},
};

constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA53, .relative_performance = 255, .name = "S905D2 ARM A53"},
};

static const pbus_metadata_t cpu_metadata[] = {
    {
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data_buffer = operating_points,
        .data_size = sizeof(operating_points),
    },
    {
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data_buffer = performance_domains,
        .data_size = sizeof(performance_domains),
    },
};

constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_GOOGLE;
  result.pid = PDEV_PID_ASTRO;
  result.did = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.metadata_list = cpu_metadata;
  result.metadata_count = countof(cpu_metadata);
  result.mmio_list = cpu_mmios;
  result.mmio_count = countof(cpu_mmios);
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

  result = pbus_.CompositeDeviceAdd(&cpu_dev, fragments, countof(fragments), 1);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d", __func__, result);
  }

  return result;
}

}  // namespace astro
