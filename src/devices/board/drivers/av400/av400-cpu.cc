// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>

#include <soc/aml-a5/a5-gpio.h>
#include <soc/aml-a5/a5-hw.h>
#include <soc/aml-a5/a5-power.h>
#include <soc/aml-common/aml-cpu-metadata.h>

#include "av400.h"
#include "src/devices/board/drivers/av400/av400-cpu-bind.h"

namespace {

static constexpr amlogic_cpu::PerfDomainId kPdArmA55 = 1;

static constexpr pbus_mmio_t cpu_mmios[] = {
    {
        .base = A5_SYS_CTRL_BASE,
        .length = A5_SYS_CTRL_LENGTH,
    },
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

static constexpr amlogic_cpu::perf_domain_t performance_domains[] = {
    {.id = kPdArmA55, .core_count = 4, .relative_performance = 255, .name = "a5-arm-a55"},
};

static const pbus_metadata_t cpu_metadata[] = {
    {
        .type = DEVICE_METADATA_AML_OP_POINTS,
        .data_buffer = reinterpret_cast<const uint8_t*>(operating_0_points),
        .data_size = sizeof(operating_0_points),
    },
    {
        .type = DEVICE_METADATA_AML_PERF_DOMAINS,
        .data_buffer = reinterpret_cast<const uint8_t*>(performance_domains),
        .data_size = sizeof(performance_domains),
    },
};

static constexpr pbus_dev_t cpu_dev = []() {
  pbus_dev_t result = {};
  result.name = "aml-cpu";
  result.vid = PDEV_VID_AMLOGIC;
  result.pid = PDEV_PID_AMLOGIC_A5;
  result.did = PDEV_DID_AMLOGIC_CPU;
  result.metadata_list = cpu_metadata;
  result.metadata_count = std::size(cpu_metadata);
  result.mmio_list = cpu_mmios;
  result.mmio_count = std::size(cpu_mmios);
  return result;
}();

}  // namespace

namespace av400 {

zx_status_t Av400::CpuInit() {
  zx_status_t result;

  result = pbus_.AddComposite(&cpu_dev, reinterpret_cast<uint64_t>(aml_cpu_fragments),
                              std::size(aml_cpu_fragments), "power-01");
  if (result != ZX_OK) {
    zxlogf(ERROR, "Failed to add CPU composite device: %s", zx_status_get_string(result));
  }

  return result;
}

}  // namespace av400
