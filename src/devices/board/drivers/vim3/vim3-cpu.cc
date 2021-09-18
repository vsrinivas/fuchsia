// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
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

namespace {

constexpr amlogic_cpu::PerfDomainId kPdArmA53 = 1;
constexpr amlogic_cpu::PerfDomainId kPdArmA73 = 2;

constexpr pbus_mmio_t cpu_mmios[]{
    {
        // AOBUS
        .base = A311D_AOBUS_BASE,
        .length = A311D_AOBUS_LENGTH,
    },
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

const pbus_metadata_t cpu_metadata[] = {
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
  result.vid = PDEV_VID_AMLOGIC;
  result.pid = PDEV_PID_AMLOGIC_A311D;
  result.did = PDEV_DID_AMLOGIC_CPU;
  result.metadata_list = cpu_metadata;
  result.metadata_count = countof(cpu_metadata);
  result.mmio_list = cpu_mmios;
  result.mmio_count = countof(cpu_mmios);
  return result;
}();

}  // namespace

namespace vim3 {

zx_status_t Vim3::CpuInit() {
  auto result = pbus_.AddComposite(&cpu_dev, reinterpret_cast<uint64_t>(vim3_cpu_fragments),
                                   countof(vim3_cpu_fragments), "power-01");
  if (result != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to add CPU composite device, st = %d", __func__, result);
  }

  return result;
}

}  // namespace vim3
