// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_CPUFREQ_H_
#define ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_CPUFREQ_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <optional>

#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/scpi.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/composite.h>
#include <hwreg/mmio.h>
#include <soc/aml-common/aml-thermal.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

namespace thermal {

namespace {

constexpr uint32_t kSysCpuWaitBusyRetries = 5;
constexpr uint32_t kSysCpuWaitBusyTimeoutUs = 10'000;

// Initial frequencies
constexpr uint32_t kSherlockBigFreqInit = 1'000'000'000;
constexpr uint32_t kSherlockLittleFreqInit = 1'200'000'000;
constexpr uint32_t kAstroFreqInit = 1'200'000'000;

// MMIO indexes.
constexpr uint32_t kHiuMmio = 2;

// 1GHz Frequency.
constexpr uint32_t kFrequencyThreshold = 1'000'000'000;

// 1.896GHz Frequency.
constexpr uint32_t kMaxCPUFrequency = 1'896'000'000;
constexpr uint32_t kMaxCPUBFrequency = 1'704'000'000;

// Final Mux for selecting clock source.
constexpr uint32_t kFixedPll = 0;
constexpr uint32_t kSysPll = 1;

constexpr uint8_t kAstroClockCount = 2;

constexpr uint8_t kAstroPwmCount = 1;
constexpr uint8_t kSherlockPwmCount = 2;

}  // namespace

// This class handles the dynamic changing of
// CPU frequency.
class AmlCpuFrequency {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlCpuFrequency);
  AmlCpuFrequency() = default;
  AmlCpuFrequency(ddk::MmioBuffer hiu_mmio, mmio_buffer_t hiu_internal_mmio, uint32_t pid)
      : hiu_mmio_(std::move(hiu_mmio)), pid_(pid) {
    // HIU Init.
    hiu_.mmio = std::move(hiu_internal_mmio);
    hiu_.regs_vaddr = static_cast<uint8_t*>(hiu_.mmio.vaddr);
  }
  ~AmlCpuFrequency() = default;
  zx_status_t SetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain, uint32_t rate);
  zx_status_t Create(zx_device_t* parent);
  zx_status_t Init();
  uint32_t GetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain);

 private:
  // CLK indexes.
  enum {
    kSysPllDiv16,
    kSysCpuClkDiv16,
    kSysPllBDiv16,
    kSysCpuBClkDiv16,
    kClockCount,
  };

  zx_status_t WaitForBusyCpu(uint32_t offset);
  zx_status_t ConfigureSysPLL(uint32_t new_rate, uint32_t offset);
  zx_status_t ConfigureSys1PLL(uint32_t new_rate, uint32_t offset);
  zx_status_t ConfigureCpuFixedPLL(uint32_t new_rate, uint32_t offset);
  zx_status_t SetBigClusterFrequency(uint32_t new_rate, uint32_t offset);
  zx_status_t SetLittleClusterFrequency(uint32_t new_rate, uint32_t offset);

  // Protocols.
  ddk::ClockProtocolClient clks_[kClockCount];
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
  // PID
  uint32_t pid_;
};
}  // namespace thermal

#endif  // ZIRCON_SYSTEM_DEV_THERMAL_AML_THERMAL_S905D2G_AML_CPUFREQ_H_
