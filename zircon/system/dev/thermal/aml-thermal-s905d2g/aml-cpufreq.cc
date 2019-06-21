// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpufreq.h"
#include "aml-fclk.h"
#include "hiu-registers.h"
#include <ddk/debug.h>
#include <unistd.h>

namespace thermal {

namespace {

#define SYS_CPU_WAIT_BUSY_RETRIES 5
#define SYS_CPU_WAIT_BUSY_TIMEOUT_US 10000

// MMIO indexes.
constexpr uint32_t kHiuMmio = 2;

// 1GHz Frequency.
constexpr uint32_t kFrequencyThreshold = 1000000000;

// 1.896GHz Frequency.
constexpr uint32_t kMaxCPUFrequency = 1896000000;

// Final Mux for selecting clock source.
constexpr uint32_t kFixedPll = 0;
constexpr uint32_t kSysPll = 1;

} // namespace

zx_status_t AmlCpuFrequency::InitPdev(zx_device_t* parent) {
    pdev_ = ddk::PDev(parent);
    if (!pdev_.is_valid()) {
        zxlogf(ERROR, "aml-cpufreq: failed to get clk protocol\n");
        return ZX_ERR_NO_RESOURCES;
    }

    // Get the clock protocols
    for (unsigned i = 0; i < kClockCount; i++) {
        clock_protocol_t clock;
        size_t actual;
        auto status = pdev_.GetProtocol(ZX_PROTOCOL_CLOCK, i, &clock, sizeof(clock), &actual);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-cpufreq: failed to get clk protocol\n");
            return status;
        }
        clks_[i] = &clock;
    }

    // Initialized the MMIOs
    zx_status_t status = pdev_.MapMmio(kHiuMmio, &hiu_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: could not map periph mmio: %d\n", status);
        return status;
    }

    // Get BTI handle.
    status = pdev_.GetBti(0, &bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: could not get BTI handle: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t AmlCpuFrequency::Init(zx_device_t* parent) {
    zx_status_t status = InitPdev(parent);
    if (status != ZX_OK) {
        return status;
    }

    // HIU Init.
    status = s905d2_hiu_init(&hiu_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: hiu_init failed: %d\n", status);
        return status;
    }

    // Enable the following clocks so we can measure them
    // and calculate what the actual CPU freq is set to at
    // any given point.
    status = clks_[kSysPllDiv16].Enable();
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to enable clock, status = %d\n", status);
        return status;
    }

    status = clks_[kSysCpuClkDiv16].Enable();
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to enable clock, status = %d\n", status);
        return status;
    }

    // Set up CPU freq. frequency to 1GHz.
    // Once we switch to using the MPLL, we re-initialize the SYS PLL
    // to known values and then the thermal driver can take over the dynamic
    // switching.
    status = SetFrequency(kFrequencyThreshold);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq, status = %d\n", status);
        return status;
    }

    // SYS PLL Init.
    status = s905d2_pll_init(&hiu_, &sys_pll_, SYS_PLL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: s905d2_pll_init failed: %d\n", status);
        return status;
    }

    // Set the SYS PLL to some known rate, before enabling the PLL.
    status = s905d2_pll_set_rate(&sys_pll_, kMaxCPUFrequency);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to set SYS_PLL rate, status = %d\n", status);
        return status;
    }

    // Enable SYS PLL.
    status = s905d2_pll_ena(&sys_pll_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: s905d2_pll_ena failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}

zx_status_t AmlCpuFrequency::WaitForBusy() {
    auto sys_cpu_ctrl0 = SysCpuClkControl0::Get().ReadFrom(&*hiu_mmio_);

    // Wait till we are not busy.
    for (uint32_t i = 0; i < SYS_CPU_WAIT_BUSY_RETRIES; i++) {
        sys_cpu_ctrl0 = SysCpuClkControl0::Get().ReadFrom(&*hiu_mmio_);
        if (sys_cpu_ctrl0.busy()) {
            // Wait a little bit before trying again.
            zx_nanosleep(zx_deadline_after(ZX_USEC(SYS_CPU_WAIT_BUSY_TIMEOUT_US)));
            continue;
        } else {
            return ZX_OK;
        }
    }
    return ZX_ERR_TIMED_OUT;
}

// NOTE: This block doesn't modify the MPLL, it just programs the muxes &
// dividers to get the new_rate in the sys_pll_div block. Refer fig. 6.6 Multi
// Phase PLLS for A53 in the datasheet.
zx_status_t AmlCpuFrequency::ConfigureFixedPLL(uint32_t new_rate) {
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

    zx_status_t status = WaitForBusy();
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to wait for busy, status = %d\n", status);
        return status;
    }

    // Now program the values into sys cpu clk control0
    auto sys_cpu_ctrl0 = SysCpuClkControl0::Get().ReadFrom(&*hiu_mmio_);

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

    current_rate_ = new_rate;
    return ZX_OK;
}

zx_status_t AmlCpuFrequency::ConfigureSysPLL(uint32_t new_rate) {
    // This API also validates if the new_rate is valid.
    // So no need to validate it here.
    zx_status_t status = s905d2_pll_set_rate(&sys_pll_, new_rate);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to set SYS_PLL rate, status = %d\n", status);
        return status;
    }

    // Now we need to change the final mux to select input as SYS_PLL.
    status = WaitForBusy();
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-cpufreq: failed to wait for busy, status = %d\n", status);
        return status;
    }

    // Select the final mux.
    auto sys_cpu_ctrl0 = SysCpuClkControl0::Get().ReadFrom(&*hiu_mmio_);
    sys_cpu_ctrl0.set_final_mux_sel(kSysPll).WriteTo(&*hiu_mmio_);

    current_rate_ = new_rate;
    return status;
}

zx_status_t AmlCpuFrequency::SetFrequency(uint32_t new_rate) {
    zx_status_t status;

    if (new_rate > kFrequencyThreshold && current_rate_ > kFrequencyThreshold) {
        // Switching between two frequencies both higher than 1GHz.
        // In this case, as per the datasheet it is recommended to change
        // to a frequency lower than 1GHz first and then switch to higher
        // frequency to avoid glitches.

        // Let's first switch to 1GHz
        status = SetFrequency(kFrequencyThreshold);
        if (status != ZX_OK) {
            zxlogf(ERROR, "aml-cpufreq: failed to set CPU freq to intermediate freq, status = %d\n",
                   status);
            return status;
        }

        // Now let's set SYS_PLL rate to new_rate.
        return ConfigureSysPLL(new_rate);

    } else if (new_rate > kFrequencyThreshold && current_rate_ <= kFrequencyThreshold) {
        // Switching from a frequency lower than 1GHz to one greater than 1GHz.
        // In this case we just need to set the SYS_PLL to required rate and
        // then set the final mux to 1 (to select SYS_PLL as the source.)

        // Now let's set SYS_PLL rate to new_rate.
        return ConfigureSysPLL(new_rate);

    } else {
        // Switching between two frequencies below 1GHz.
        // In this case we change the source and dividers accordingly
        // to get the required rate from MPLL and do not touch the
        // final mux.
        return ConfigureFixedPLL(new_rate);
    }
    return ZX_OK;
}

uint32_t AmlCpuFrequency::GetFrequency() {
    return current_rate_;
}

} // namespace thermal
