// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_CPUFREQ_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_CPUFREQ_H_

#include <fuchsia/hardware/clock/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <optional>

#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <hwreg/mmio.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

namespace thermal {

// This class handles the dynamic changing of
// CPU frequency.
class AmlCpuFrequency {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpuFrequency);
  AmlCpuFrequency() = default;
  AmlCpuFrequency(ddk::MmioBuffer hiu_mmio, mmio_buffer_t hiu_internal_mmio,
                  const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                  const aml_thermal_info_t& thermal_info)
      : hiu_mmio_(std::move(hiu_mmio)),
        big_cluster_current_rate_(
            thermal_info.initial_cluster_frequencies
                [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN]),
        little_cluster_current_rate_(
            thermal_info.initial_cluster_frequencies
                [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN]),
        big_little_(thermal_config.big_little) {
    // HIU Init.
    hiu_.mmio = std::move(hiu_internal_mmio);
    hiu_.regs_vaddr = static_cast<MMIO_PTR uint8_t*>(hiu_.mmio.vaddr);
  }
  ~AmlCpuFrequency() = default;
  zx_status_t SetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain, uint32_t rate);
  zx_status_t Create(zx_device_t* parent,
                     const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                     const aml_thermal_info_t& thermal_info);

  zx_status_t Init();
  uint32_t GetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain);

 private:
  zx_status_t WaitForBusyCpu(uint32_t offset);
  zx_status_t ConfigureSysPLL(uint32_t new_rate, uint32_t offset);
  zx_status_t ConfigureSys1PLL(uint32_t new_rate, uint32_t offset);
  zx_status_t ConfigureCpuFixedPLL(uint32_t new_rate, uint32_t offset);
  zx_status_t SetBigClusterFrequency(uint32_t new_rate, uint32_t offset);
  zx_status_t SetLittleClusterFrequency(uint32_t new_rate, uint32_t offset);

  // MMIOS.
  std::optional<ddk::MmioBuffer> hiu_mmio_;
  // HIU Handle.
  aml_hiu_dev_t hiu_;
  // Sys PLL.
  aml_pll_dev_t sys_pll_;
  // Sys1 PLL.
  aml_pll_dev_t sys1_pll_;
  // Current Frequency, default is 1.2GHz,
  // which is set by u-boot while booting up.
  uint32_t big_cluster_current_rate_;
  uint32_t little_cluster_current_rate_;
  bool big_little_ = false;
};
}  // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_LEGACY_AML_CPUFREQ_H_
