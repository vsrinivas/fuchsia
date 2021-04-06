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
#include <fuchsia/amlogic/cpu/metadata/llcpp/fidl.h>

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

zx_status_t GenerateMetadata() {

  return ZX_OK;
}

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

  constexpr size_t kNumOperatingPoints = 11;
  const fidl::StringView cluster_name = "S905D2 ARM A53";

  fidl::FidlAllocator allocator;
  fuchsia_amlogic_cpu_metadata::wire::AmlogicCpuMetadata metadata(allocator);
  fuchsia_amlogic_cpu_metadata::wire::AmlogicCpuPerformanceDomain perf_domain(allocator);
  fidl::VectorView<fuchsia_amlogic_cpu_metadata::wire::OperatingPoint> operating_points_v(allocator, kNumOperatingPoints);

  for(auto& opt : operating_points_v) {
    opt.Allocate(allocator);
  }

  operating_points_v[0].set_frequency(allocator, 100'000'000).set_voltage(allocator, 731'000);
  operating_points_v[1].set_frequency(allocator, 250'000'000).set_voltage(allocator, 731'000);
  operating_points_v[2].set_frequency(allocator, 500'000'000).set_voltage(allocator, 731'000);
  operating_points_v[3].set_frequency(allocator, 667'000'000).set_voltage(allocator, 731'000);
  operating_points_v[4].set_frequency(allocator, 1'000'000'000).set_voltage(allocator, 731'000);
  operating_points_v[5].set_frequency(allocator, 1'200'000'000).set_voltage(allocator, 731'000);
  operating_points_v[6].set_frequency(allocator, 1'398'000'000).set_voltage(allocator, 761'000);
  operating_points_v[7].set_frequency(allocator, 1'512'000'000).set_voltage(allocator, 791'000);
  operating_points_v[8].set_frequency(allocator, 1'608'000'000).set_voltage(allocator, 831'000);
  operating_points_v[9].set_frequency(allocator, 1'704'000'000).set_voltage(allocator, 861'000);
  operating_points_v[10].set_frequency(allocator, 1'896'000'000).set_voltage(allocator, 981'000);

  perf_domain.set_operating_points(allocator, std::move(operating_points_v))
    .set_id(allocator, kPdArmA53)
    .set_core_count(allocator, 4)
    .set_relative_performance(allocator, 255)
    .set_name(allocator, std::move(cluster_name));

  fidl::VectorView<fuchsia_amlogic_cpu_metadata::wire::AmlogicCpuPerformanceDomain> perf_domains(allocator, 1);
  perf_domains[0] = std::move(perf_domain);

  metadata.set_domains(allocator, std::move(perf_domains));

  fidl::OwnedEncodedMessage<fuchsia_amlogic_cpu_metadata::wire::AmlogicCpuMetadata> encoded(&metadata);
  if (!encoded.ok()) {
    return ZX_ERR_INTERNAL;
  }

  fidl_outgoing_msg_t* message = encoded.GetOutgoingMessage().message();
  if (message->type != FIDL_OUTGOING_MSG_TYPE_BYTE) {
    zxlogf(ERROR, "Encoded message type should be byte, not iovec");
    return ZX_ERR_INTERNAL;
  }

  const pbus_metadata_t cpu_metadata[] = {
    {
      .type = DEVICE_METADATA_AML_CPU,
      .data_buffer = reinterpret_cast<const uint8_t*>(message->byte.bytes),
      .data_size = message->byte.num_bytes,
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
