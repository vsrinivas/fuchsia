// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-meson/g12a-clk.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-power.h>

#include <src/devices/lib/amlogic/snapshot/metadata_generated.h>

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

pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_GOOGLE;
  result.pid = PDEV_PID_ASTRO;
  result.did = PDEV_DID_GOOGLE_AMLOGIC_CPU;
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

  Amlogic::Metadata::AmlogicCpuMetadataT metadata;

  std::vector<std::unique_ptr<Amlogic::Metadata::AmlogicCpuPerformanceDomainT>> domains(1);
  domains[0] = std::make_unique<Amlogic::Metadata::AmlogicCpuPerformanceDomainT>();

  domains[0]->core_count = 4;
  domains[0]->id = kPdArmA53;
  domains[0]->relative_performance = 255;
  domains[0]->name = "S905D2 ARM A53";

  std::vector<std::unique_ptr<Amlogic::Metadata::OperatingPointT>> ops(11);
  for (size_t i = 0; i < ops.size(); i++) {
    ops[i] = std::make_unique<Amlogic::Metadata::OperatingPointT>();
  }
  ops[0]->frequency  = 100000000;  ops[0]->voltage  = 731000;
  ops[1]->frequency  = 250000000;  ops[1]->voltage  = 731000;
  ops[2]->frequency  = 500000000;  ops[2]->voltage  = 731000;
  ops[3]->frequency  = 667000000;  ops[3]->voltage  = 731000;
  ops[4]->frequency  = 1000000000; ops[4]->voltage  = 731000;
  ops[5]->frequency  = 1200000000; ops[5]->voltage  = 731000;
  ops[6]->frequency  = 1398000000; ops[6]->voltage  = 761000;
  ops[7]->frequency  = 1512000000; ops[7]->voltage  = 791000;
  ops[8]->frequency  = 1608000000; ops[8]->voltage  = 831000;
  ops[9]->frequency  = 1704000000; ops[9]->voltage  = 861000;
  ops[10]->frequency = 1896000000; ops[10]->voltage = 981000;

  domains[0]->operating_points = std::move(ops);

  metadata.domains = std::move(domains);

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(Amlogic::Metadata::AmlogicCpuMetadata::Pack(fbb, &metadata));

  const pbus_metadata_t cpu_metadata[] = {
    {
      .type = DEVICE_METADATA_AML_CPU,
      .data_buffer = reinterpret_cast<const uint8_t*>(fbb.GetBufferPointer()),
      .data_size = fbb.GetSize(),
    }
  };

  cpu_dev.metadata_list = cpu_metadata;
  cpu_dev.metadata_count = countof(cpu_metadata);

  result = pbus_.CompositeDeviceAdd(&cpu_dev, reinterpret_cast<uint64_t>(fragments),
                                    countof(fragments), 1);
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d", __func__, result);
  }

  return result;
}

}  // namespace astro
