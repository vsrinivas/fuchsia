// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm.h"
#include "aml-pwm-regs.h"
#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <threads.h>
#include <unistd.h>

namespace thermal {

// MMIO index.
static constexpr uint32_t kPwmMmio = 3;

// Input clock frequency
static constexpr uint32_t kXtalFreq = 24000000;

zx_status_t AmlPwm::Init(zx_device_t* parent) {
    zx_status_t status = device_get_protocol(parent,
                                             ZX_PROTOCOL_PLATFORM_DEV,
                                             &pdev_);
    if (status != ZX_OK) {
        return status;
    }

    // Map amlogic pwm registers
    status = pdev_map_mmio_buffer(&pdev_, kPwmMmio, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &pwm_mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml-pwm: could not map periph mmio: %d\n", status);
        return status;
    }

    pwm_regs_ = fbl::make_unique<hwreg::RegisterIo>(reinterpret_cast<volatile void*>(
        io_buffer_virt(&pwm_mmio_)));

    switch (hwpwm_) {
    case 0:
        pwm_duty_cycle_offset_ = S905D2_AO_PWM_PWM_A;
        enable_bit_ = A_ENABLE;
        clk_enable_bit_ = CLK_A_ENABLE;
        break;
    case 1:
        pwm_duty_cycle_offset_ = S905D2_AO_PWM_PWM_B;
        enable_bit_ = B_ENABLE;
        clk_enable_bit_ = CLK_B_ENABLE;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t AmlPwm::Configure(uint32_t duty_cycle) {
    uint16_t low_count;
    uint16_t high_count;
    uint64_t fin_ns = NSEC_PER_SEC / kXtalFreq;

    if (duty_cycle > 100) {
        return ZX_ERR_INVALID_ARGS;
    }

    // If the current duty cycle is the same as requested, no need to do anything.
    if (duty_cycle == duty_cycle_) {
        return ZX_OK;
    }

    // Calculate the high and low count first based on the duty cycle requested.
    uint32_t duty = (duty_cycle * period_) / 100;
    uint16_t count = static_cast<uint16_t>(period_ / fin_ns);

    if (duty == period_) {
        high_count = count;
        low_count = 0;
    } else if (duty == 0) {
        high_count = 0;
        low_count = count;
    } else {
        uint16_t duty_count = static_cast<uint16_t>(duty / fin_ns);
        high_count = duty_count;
        low_count = static_cast<uint16_t>(count - duty_count);
    }

    fbl::AutoLock lock(&pwm_lock_);

    uint32_t value = (high_count << PWM_HIGH_SHIFT) | low_count;
    pwm_regs_->Write(pwm_duty_cycle_offset_, value);

    value = pwm_regs_->Read<uint32_t>(S905D2_AO_PWM_MISC_REG_AB);
    value |= enable_bit_ | clk_enable_bit_;
    pwm_regs_->Write(S905D2_AO_PWM_MISC_REG_AB, value);

    // Update the new duty_cycle information
    duty_cycle_ = duty_cycle;
    return ZX_OK;
}

AmlPwm::~AmlPwm() {
    io_buffer_release(&pwm_mmio_);
}

} // namespace thermal
