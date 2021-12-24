// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-common/aml-cpu-metadata.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-hw.h>
#include <soc/aml-t931/t931-power.h>

#include "sherlock.h"
#include "src/devices/board/drivers/sherlock/luis-cpu-bind.h"

namespace {

constexpr amlogic_cpu::PerfDomainId kPdArmA53 = 1;
constexpr amlogic_cpu::PerfDomainId kPdArmA73 = 2;

constexpr pbus_mmio_t cpu_mmios[]{
    {
        // AOBUS
        .base = T931_AOBUS_BASE,
        .length = T931_AOBUS_LENGTH,
    },
};

constexpr amlogic_cpu::operating_point_t operating_points[] = {
    // Little Cluster DVFS Table
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

    // Big Cluster DVFS Table.
    {.freq_hz = 100'000'000, .volt_uv = 737'500, .pd_id = kPdArmA73},
    {.freq_hz = 250'000'000, .volt_uv = 737'500, .pd_id = kPdArmA73},
    {.freq_hz = 500'000'000, .volt_uv = 737'500, .pd_id = kPdArmA73},
    {.freq_hz = 667'000'000, .volt_uv = 737'500, .pd_id = kPdArmA73},
    {.freq_hz = 1000'000'000, .volt_uv = 737'500, .pd_id = kPdArmA73},
    {.freq_hz = 1200'000'000, .volt_uv = 750'000, .pd_id = kPdArmA73},
    {.freq_hz = 1398'000'000, .volt_uv = 775'000, .pd_id = kPdArmA73},
    {.freq_hz = 1512'000'000, .volt_uv = 775'000, .pd_id = kPdArmA73},
    {.freq_hz = 1608'000'000, .volt_uv = 787'500, .pd_id = kPdArmA73},
    {.freq_hz = 1704'000'000, .volt_uv = 800'000, .pd_id = kPdArmA73},
    {.freq_hz = 1800'000'000, .volt_uv = 837'500, .pd_id = kPdArmA73},
    {.freq_hz = 1908'000'000, .volt_uv = 862'500, .pd_id = kPdArmA73},
    {.freq_hz = 2016'000'000, .volt_uv = 912'500, .pd_id = kPdArmA73},
    {.freq_hz = 2100'000'000, .volt_uv = 950'000, .pd_id = kPdArmA73},
    {.freq_hz = 2208'000'000, .volt_uv = 1012'500, .pd_id = kPdArmA73},
};

constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA73, .relative_performance = 255, .name = "t931-arm-a73"},
    {.id = kPdArmA53, .relative_performance = 128, .name = "t931-arm-a53"},
};

static const pbus_metadata_t cpu_metadata[] = {
    {
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data_buffer = reinterpret_cast<const uint8_t*>(operating_points),
        .data_size = sizeof(operating_points),
    },
    {
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data_buffer = reinterpret_cast<const uint8_t*>(performance_domains),
        .data_size = sizeof(performance_domains),
    },
};

constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_GOOGLE;
  result.pid = PDEV_PID_LUIS;
  result.did = PDEV_DID_GOOGLE_AMLOGIC_CPU;
  result.metadata_list = cpu_metadata;
  result.metadata_count = std::size(cpu_metadata);
  result.mmio_list = cpu_mmios;
  result.mmio_count = std::size(cpu_mmios);
  return result;
}();

}  // namespace

namespace sherlock {

zx_status_t Sherlock::LuisCpuInit() {
  zx_status_t result = pbus_.AddComposite(&cpu_dev, reinterpret_cast<uint64_t>(aml_cpu_fragments),
                                          std::size(aml_cpu_fragments), "power-01");

  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d\n", __func__, result);
  }

  return result;
}

}  // namespace sherlock
