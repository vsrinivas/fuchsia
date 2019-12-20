// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm.h"

#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "aml-pwm-regs.h"

namespace {

constexpr uint64_t DivideRounded(uint64_t num, uint64_t denom) {
  return (num + (denom / 2)) / denom;
}

}  // namespace

namespace thermal {

zx_status_t AmlPwm::Create(zx_device_t* parent, PwmType pwm_type) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "aml-pwm: failed to get pdev protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  switch (pwm_type) {
    case PWM_AO_CD: {
      // Map amlogic pwm registers
      zx_status_t status = pdev.MapMmio(kPwmAOCDMmio, &pwm_mmio_);
      if (status != ZX_OK) {
        zxlogf(ERROR, "aml-pwm: could not map periph mmio: %d\n", status);
        return status;
      }
      break;
    }
    case PWM_AB: {
      // Map amlogic pwm registers
      zx_status_t status = pdev.MapMmio(kPwmABMmio, &pwm_mmio_);
      if (status != ZX_OK) {
        zxlogf(ERROR, "aml-pwm: could not map periph mmio: %d\n", status);
        return status;
      }
      break;
    }
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t AmlPwm::Init(uint32_t period_ns, uint32_t hwpwm) {
  period_ns_ = period_ns;

  switch (hwpwm) {
    case 0:
      pwm_duty_cycle_offset_ = S905D2_AO_PWM_PWM_A;
      enable_bit_ = A_ENABLE;
      clk_enable_bit_ = CLK_A_ENABLE;
      constant_enable_bit_ = A_CONSTANT_ENABLE;
      break;
    case 1:
      pwm_duty_cycle_offset_ = S905D2_AO_PWM_PWM_B;
      enable_bit_ = B_ENABLE;
      clk_enable_bit_ = CLK_B_ENABLE;
      constant_enable_bit_ = B_CONSTANT_ENABLE;
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t AmlPwm::Configure(uint32_t duty_cycle) {
  constexpr uint64_t kNanosecondsPerClock = NSEC_PER_SEC / kXtalFreq;

  if (duty_cycle > 100) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If the current duty cycle is the same as requested, no need to do anything.
  if (duty_cycle == duty_cycle_) {
    return ZX_OK;
  }

  // Calculate the high and low count first based on the duty cycle requested.
  const uint32_t high_time_ns = (duty_cycle * period_ns_) / 100;
  const uint16_t period_count = static_cast<uint16_t>(period_ns_ / kNanosecondsPerClock);

  const uint16_t duty_count =
      static_cast<uint16_t>(DivideRounded(high_time_ns, kNanosecondsPerClock));

  uint16_t high_count = duty_count;
  uint16_t low_count = static_cast<uint16_t>(period_count - duty_count);
  if (duty_count != period_count && duty_count != 0) {
    high_count--;
    low_count--;
  }

  fbl::AutoLock lock(&pwm_lock_);

  pwm_mmio_->Write32((high_count << PWM_HIGH_SHIFT) | low_count, pwm_duty_cycle_offset_);

  const uint32_t kMiscBitMask = enable_bit_ | clk_enable_bit_ | constant_enable_bit_;
  if (duty_count == period_count || duty_count == 0) {
    pwm_mmio_->SetBits32(kMiscBitMask, S905D2_AO_PWM_MISC_REG_AB);
  } else {
    pwm_mmio_->ModifyBits32(enable_bit_ | clk_enable_bit_, kMiscBitMask, S905D2_AO_PWM_MISC_REG_AB);
  }

  // Update the new duty_cycle information
  duty_cycle_ = duty_cycle;

  return ZX_OK;
}

}  // namespace thermal
