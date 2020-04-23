// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpufreq.h"

#include <unistd.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <fbl/algorithm.h>

#include "aml-fclk.h"
#include "hiu-registers.h"

namespace {

constexpr uint32_t kSysCpuWaitBusyRetries = 5;
constexpr uint32_t kSysCpuWaitBusyTimeoutUs = 10'000;

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

constexpr uint32_t kPwmsPerCluster = 1;
constexpr uint32_t kClocksPerCluster = 2;

}  // namespace

namespace thermal {

zx_status_t AmlCpuFrequency::Create(
    zx_device_t* parent, const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
    const aml_thermal_info_t& thermal_info) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "aml-cpufreq: failed to get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  big_little_ = thermal_config.big_little;
  big_cluster_current_rate_ = thermal_info.initial_cluster_frequencies
                                  [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN];
  little_cluster_current_rate_ =
      thermal_info.initial_cluster_frequencies
          [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN];

  constexpr size_t kMaxFragments =
      ((kPwmsPerCluster + kClocksPerCluster) * fuchsia_hardware_thermal_MAX_DVFS_DOMAINS) + 1;

  const size_t num_clocks = kClocksPerCluster * (big_little_ ? 2 : 1);
  const size_t num_pwms = kPwmsPerCluster * (big_little_ ? 2 : 1);

  // zeroth fragment is pdev
  zx_device_t* fragments[kMaxFragments];
  size_t actual = 0;
  composite.GetFragments(fragments, fbl::count_of(fragments), &actual);

  if (actual < (num_clocks + num_pwms + 1)) {
    zxlogf(ERROR, "aml-cpufreq: not enough fragments");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::PDev pdev(fragments[0]);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-cpufreq: failed to get pdev protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Initialized the MMIOs
  zx_status_t status = pdev.MapMmio(kHiuMmio, &hiu_mmio_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: could not map periph mmio: %d", status);
    return status;
  }

  // HIU Init.
  status = s905d2_hiu_init(&hiu_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: hiu_init failed: %d", status);
    return status;
  }

  // Enable the following clocks so we can measure them
  // and calculate what the actual CPU freq is set to at
  // any given point.

  for (size_t i = 0; i < num_clocks; i++) {
    ddk::ClockProtocolClient clock;
    status = ddk::ClockProtocolClient::CreateFromDevice(fragments[num_pwms + i + 1], &clock);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to get clk protocol");
      return status;
    }

    status = clock.Enable();
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to enable clock, status = %d", status);
      return status;
    }
  }

  return Init();
}

zx_status_t AmlCpuFrequency::Init() {
  // Set up CPU freq. frequency to 1GHz.
  // Once we switch to using the MPLL, we re-initialize the SYS PLL
  // to known values and then the thermal driver can take over the dynamic
  // switching.
  zx_status_t status = SetFrequency(fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN,
                                    kFrequencyThreshold);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq, status = %d", status);
    return status;
  }

  if (big_little_) {
    status = SetFrequency(fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
                          kFrequencyThreshold);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq, status = %d", status);
      return status;
    }
  }

  // SYS PLL Init.
  status = s905d2_pll_init(&hiu_, &sys_pll_, SYS_PLL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: s905d2_pll_init failed: %d", status);
    return status;
  }

  // Set the SYS PLL to some known rate, before enabling the PLL.
  status = s905d2_pll_set_rate(&sys_pll_, kMaxCPUBFrequency);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to set SYS_PLL rate, status = %d", status);
    return status;
  }

  // Enable SYS PLL.
  status = s905d2_pll_ena(&sys_pll_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: s905d2_pll_ena failed: %d", status);
    return status;
  }

  if (big_little_) {
    // SYS1 PLL Init.
    status = s905d2_pll_init(&hiu_, &sys1_pll_, SYS1_PLL);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: s905d2_pll_init failed: %d", status);
      return status;
    }

    // Set the SYS1 PLL to some known rate, before enabling the PLL.
    status = s905d2_pll_set_rate(&sys1_pll_, kMaxCPUFrequency);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to set SYS1_PLL rate, status = %d", status);
      return status;
    }

    // Enable SYS1 PLL.
    status = s905d2_pll_ena(&sys1_pll_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: s905d2_pll_ena failed: %d", status);
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t AmlCpuFrequency::WaitForBusyCpu(uint32_t offset) {
  auto sys_cpu_ctrl0 = SysCpuClkControl0::Get(offset).ReadFrom(&*hiu_mmio_);

  // Wait till we are not busy.
  for (uint32_t i = 0; i < kSysCpuWaitBusyRetries; i++) {
    sys_cpu_ctrl0 = SysCpuClkControl0::Get(offset).ReadFrom(&*hiu_mmio_);

    if (sys_cpu_ctrl0.busy()) {
      // Wait a little bit before trying again.
      zx_nanosleep(zx_deadline_after(ZX_USEC(kSysCpuWaitBusyTimeoutUs)));
      continue;
    } else {
      return ZX_OK;
    }
  }
  return ZX_ERR_TIMED_OUT;
}

// NOTE: This block doesn't modify the MPLL, it just programs the muxes &
// dividers to get the new_rate in the sys_pll_div block. Refer fig. 6.6 Multi
// Phase PLLS for A53 & A73 in the datasheet.
zx_status_t AmlCpuFrequency::ConfigureCpuFixedPLL(uint32_t new_rate, uint32_t offset) {
  const aml_fclk_rate_table_t* fclk_rate_table = s905d2_fclk_get_rate_table();
  size_t rate_count = s905d2_fclk_get_rate_table_count();
  size_t i;

  // Validate if the new_rate is available
  for (i = 0; i < rate_count; i++) {
    if (new_rate == fclk_rate_table[i].rate) {
      break;
    }
  }
  if (i == rate_count) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = WaitForBusyCpu(offset);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to wait for busy, status = %d", status);
    return status;
  }

  auto sys_cpu_ctrl0 = SysCpuClkControl0::Get(offset).ReadFrom(&*hiu_mmio_);

  if (sys_cpu_ctrl0.final_dyn_mux_sel()) {
    // Dynamic mux 1 is in use, we setup dynamic mux 0
    sys_cpu_ctrl0.set_final_dyn_mux_sel(0)
        .set_mux0_divn_tcnt(fclk_rate_table[i].mux_div)
        .set_postmux0(fclk_rate_table[i].postmux)
        .set_premux0(fclk_rate_table[i].premux);
  } else {
    // Dynamic mux 0 is in use, we setup dynamic mux 1
    sys_cpu_ctrl0.set_final_dyn_mux_sel(1)
        .set_mux1_divn_tcnt(fclk_rate_table[i].mux_div)
        .set_postmux1(fclk_rate_table[i].postmux)
        .set_premux1(fclk_rate_table[i].premux);
  }

  // Select the final mux.
  sys_cpu_ctrl0.set_final_mux_sel(kFixedPll).WriteTo(&*hiu_mmio_);

  return ZX_OK;
}

zx_status_t AmlCpuFrequency::ConfigureSys1PLL(uint32_t new_rate, uint32_t offset) {
  // This API also validates if the new_rate is valid.
  // So no need to validate it here.
  zx_status_t status = s905d2_pll_set_rate(&sys1_pll_, new_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to set SYS1_PLL rate, status = %d", status);
    return status;
  }

  // Now we need to change the final mux to select input as SYS_PLL.
  status = WaitForBusyCpu(offset);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to wait for busy, status = %d", status);
    return status;
  }

  // Select the final mux.
  auto sys_cpub_ctrl0 = SysCpuClkControl0::Get(offset).ReadFrom(&*hiu_mmio_);
  sys_cpub_ctrl0.set_final_mux_sel(kSysPll).WriteTo(&*hiu_mmio_);

  return status;
}

zx_status_t AmlCpuFrequency::ConfigureSysPLL(uint32_t new_rate, uint32_t offset) {
  // This API also validates if the new_rate is valid.
  // So no need to validate it here.
  zx_status_t status = s905d2_pll_set_rate(&sys_pll_, new_rate);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to set SYS_PLL rate, status = %d", status);
    return status;
  }

  // Now we need to change the final mux to select input as SYS_PLL.
  status = WaitForBusyCpu(offset);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpufreq: failed to wait for busy, status = %d", status);
    return status;
  }

  // Select the final mux.
  auto sys_cpu_ctrl0 = SysCpuClkControl0::Get(offset).ReadFrom(&*hiu_mmio_);
  sys_cpu_ctrl0.set_final_mux_sel(kSysPll).WriteTo(&*hiu_mmio_);

  return status;
}

zx_status_t AmlCpuFrequency::SetBigClusterFrequency(uint32_t new_rate, uint32_t offset) {
  zx_status_t status;

  if (new_rate > kFrequencyThreshold && big_cluster_current_rate_ > kFrequencyThreshold) {
    // Switching between two frequencies both higher than 1GHz.
    // In this case, as per the datasheet it is recommended to change
    // to a frequency lower than 1GHz first and then switch to higher
    // frequency to avoid glitches.

    // Let's first switch to 1GHz
    status = SetBigClusterFrequency(kFrequencyThreshold, offset);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq to intermediate freq, status = %d",
             status);
      return status;
    }

    // Now let's set SYS_PLL rate to new_rate.
    return ConfigureSysPLL(new_rate, offset);

  } else if (new_rate > kFrequencyThreshold && big_cluster_current_rate_ <= kFrequencyThreshold) {
    // Switching from a frequency lower than 1GHz to one greater than 1GHz.
    // In this case we just need to set the SYS_PLL to required rate and
    // then set the final mux to 1 (to select SYS_PLL as the source.)

    // Now let's set SYS_PLL rate to new_rate.
    return ConfigureSysPLL(new_rate, offset);

  } else {
    // Switching between two frequencies below 1GHz.
    // In this case we change the source and dividers accordingly
    // to get the required rate from MPLL and do not touch the
    // final mux.
    return ConfigureCpuFixedPLL(new_rate, offset);
  }
  return ZX_OK;
}

zx_status_t AmlCpuFrequency::SetLittleClusterFrequency(uint32_t new_rate, uint32_t offset) {
  zx_status_t status;

  if (new_rate > kFrequencyThreshold && little_cluster_current_rate_ > kFrequencyThreshold) {
    // Switching between two frequencies both higher than 1GHz.
    // In this case, as per the datasheet it is recommended to change
    // to a frequency lower than 1GHz first and then switch to higher
    // frequency to avoid glitches.

    // Let's first switch to 1GHz
    status = SetLittleClusterFrequency(kFrequencyThreshold, offset);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq to intermediate freq, status = %d",
             status);
      return status;
    }

    // Now let's set SYS_PLL rate to new_rate.
    return ConfigureSys1PLL(new_rate, offset);

  } else if (new_rate > kFrequencyThreshold &&
             little_cluster_current_rate_ <= kFrequencyThreshold) {
    // Switching from a frequency lower than 1GHz to one greater than 1GHz.
    // In this case we just need to set the SYS_PLL to required rate and
    // then set the final mux to 1 (to select SYS_PLL as the source.)

    // Now let's set SYS1_PLL rate to new_rate.
    return ConfigureSys1PLL(new_rate, offset);

  } else {
    // Switching between two frequencies below 1GHz.
    // In this case we change the source and dividers accordingly
    // to get the required rate from MPLL and do not touch the
    // final mux.
    return ConfigureCpuFixedPLL(new_rate, offset);
  }
  return ZX_OK;
}

zx_status_t AmlCpuFrequency::SetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain,
                                          uint32_t new_rate) {
  if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
    const uint32_t offset = big_little_ ? kSysCpuBOffset : kSysCpuOffset;
    zx_status_t status = SetBigClusterFrequency(new_rate, offset);
    if (status != ZX_OK) {
      return status;
    }
    big_cluster_current_rate_ = new_rate;
    return status;
  } else if (power_domain == fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN) {
    if (!big_little_) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = SetLittleClusterFrequency(new_rate, kSysCpuOffset);
    if (status != ZX_OK) {
      return status;
    }
    little_cluster_current_rate_ = new_rate;
    return status;
  } else
    return ZX_ERR_INVALID_ARGS;
}

uint32_t AmlCpuFrequency::GetFrequency(fuchsia_hardware_thermal_PowerDomain power_domain) {
  if (power_domain == fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN) {
    return big_cluster_current_rate_;
  } else if (power_domain == fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN) {
    return little_cluster_current_rate_;
  } else
    return ZX_ERR_INVALID_ARGS;
}

}  // namespace thermal
