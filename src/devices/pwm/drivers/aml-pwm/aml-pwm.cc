// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/protocol/platform/bus.h>
#include <soc/aml-a113/a113-pwm.h>
#include <soc/aml-s905d2/s905d2-pwm.h>
#include <soc/aml-t931/t931-pwm.h>

namespace pwm {

namespace {

// Input clock frequency
constexpr uint32_t kXtalFreq = 24'000'000;
// Nanoseconds per second
constexpr uint32_t kNsecPerSec = 1'000'000'000;

constexpr uint64_t DivideRounded(uint64_t num, uint64_t denom) {
  return (num + (denom / 2)) / denom;
}

void DutyCycleToClockCount(float duty_cycle, uint32_t period_ns, uint16_t* high_count,
                           uint16_t* low_count) {
  constexpr uint64_t kNanosecondsPerClock = kNsecPerSec / kXtalFreq;

  // Calculate the high and low count first based on the duty cycle requested.
  const auto high_time_ns =
      static_cast<uint64_t>((duty_cycle * static_cast<float>(period_ns)) / 100.0);
  const auto period_count = static_cast<uint16_t>(period_ns / kNanosecondsPerClock);

  const auto duty_count = static_cast<uint16_t>(DivideRounded(high_time_ns, kNanosecondsPerClock));

  *high_count = duty_count;
  *low_count = static_cast<uint16_t>(period_count - duty_count);
  if (duty_count != period_count && duty_count != 0) {
    (*high_count)--;
    (*low_count)--;
  }
}

zx_status_t CopyConfig(pwm_config_t* dest, const pwm_config_t* src) {
  if (!dest->mode_config_buffer || (dest->mode_config_size != src->mode_config_size) ||
      (dest->mode_config_size != sizeof(mode_config))) {
    return ZX_ERR_BAD_STATE;
  }
  dest->polarity = src->polarity;
  dest->period_ns = src->period_ns;
  dest->duty_cycle = src->duty_cycle;
  memcpy(dest->mode_config_buffer, src->mode_config_buffer, src->mode_config_size);
  return ZX_OK;
}

}  // namespace

zx_status_t AmlPwm::PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config) {
  if (idx > 1) {
    return ZX_ERR_INVALID_ARGS;
  }
  return CopyConfig(out_config, &configs_[idx]);
}

zx_status_t AmlPwm::PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
  auto mode_cfg = static_cast<const mode_config*>(config->mode_config_buffer);
  Mode mode = static_cast<Mode>(mode_cfg->mode);
  if (idx > 1 || mode >= UNKNOWN) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status;
  // Save old config
  mode_config tmp_cfg = {UNKNOWN, {}};
  pwm_config_t old_config = {false, 0, 0.0, &tmp_cfg, sizeof(mode_config)};
  if ((status = CopyConfig(&old_config, &configs_[idx])) != ZX_OK) {
    zxlogf(ERROR, "%s: could not save old config %d", __func__, status);
    return status;
  }
  auto old_mode_cfg = static_cast<const mode_config*>(old_config.mode_config_buffer);

  // Update new
  if ((status = CopyConfig(&configs_[idx], config)) != ZX_OK) {
    zxlogf(ERROR, "%s: could not save new config %d", __func__, status);
    return status;
  }

  bool mode_eq = (old_mode_cfg->mode == mode);
  if (!mode_eq && ((status = SetMode(idx, mode)) != ZX_OK)) {
    zxlogf(ERROR, "%s: Set Mode failed %d", __func__, status);
    PwmImplSetConfig(idx, &old_config);
    return status;
  }

  bool en_const = (config->duty_cycle == 0 || config->duty_cycle == 100);
  bool val_eq;
  switch (mode) {
    case OFF:
      return ZX_OK;
    case ON:
      break;
    case DELTA_SIGMA:
      val_eq = (old_mode_cfg->delta_sigma.delta == mode_cfg->delta_sigma.delta);
      if (!(mode_eq && val_eq) &&
          ((status = SetDSSetting(idx, mode_cfg->delta_sigma.delta)) != ZX_OK)) {
        zxlogf(ERROR, "%s: Set Delta Sigma Setting failed %d", __func__, status);
        PwmImplSetConfig(idx, &old_config);
        return status;
      }
      break;
    case TWO_TIMER:
      en_const = (en_const || mode_cfg->two_timer.duty_cycle2 == 0 ||
                  mode_cfg->two_timer.duty_cycle2 == 100);

      val_eq = (old_mode_cfg->two_timer.period_ns2 == mode_cfg->two_timer.period_ns2) &&
               (old_mode_cfg->two_timer.duty_cycle2 == mode_cfg->two_timer.duty_cycle2);
      if (!(mode_eq && val_eq) &&
          ((status = SetDutyCycle2(idx, mode_cfg->two_timer.period_ns2,
                                   mode_cfg->two_timer.duty_cycle2)) != ZX_OK)) {
        zxlogf(ERROR, "%s: Set Duty Cycle 2 failed %d", __func__, status);
        PwmImplSetConfig(idx, &old_config);
        return status;
      }
      val_eq = (old_mode_cfg->two_timer.timer1 == mode_cfg->two_timer.timer1) &&
               (old_mode_cfg->two_timer.timer2 == mode_cfg->two_timer.timer2);
      if (!(mode_eq && val_eq) && ((status = SetTimers(idx, mode_cfg->two_timer.timer1,
                                                       mode_cfg->two_timer.timer2)) != ZX_OK)) {
        zxlogf(ERROR, "%s: Set Timers failed %d", __func__, status);
        PwmImplSetConfig(idx, &old_config);
        return status;
      }
      break;
    case UNKNOWN:
    default:
      zxlogf(ERROR, "%s: Unsupported Mode %d", __func__, mode);
      return ZX_ERR_NOT_SUPPORTED;
  }

  val_eq = (old_config.polarity == config->polarity);
  if (!(mode_eq && val_eq) && ((status = Invert(idx, config->polarity)) != ZX_OK)) {
    zxlogf(ERROR, "%s: Invert failed %d", __func__, status);
    PwmImplSetConfig(idx, &old_config);
    return status;
  }
  if ((status = EnableConst(idx, en_const)) != ZX_OK) {
    zxlogf(ERROR, "%s: Enable Const failed %d", __func__, status);
    PwmImplSetConfig(idx, &old_config);
    return status;
  }
  val_eq =
      (old_config.period_ns == config->period_ns) && (old_config.duty_cycle == config->duty_cycle);
  if (!(mode_eq && val_eq) &&
      ((status = SetDutyCycle(idx, config->period_ns, config->duty_cycle)) != ZX_OK)) {
    zxlogf(ERROR, "%s: Set Duty Cycle failed %d", __func__, status);
    PwmImplSetConfig(idx, &old_config);
    return status;
  }

  return ZX_OK;
}

zx_status_t AmlPwm::PwmImplEnable(uint32_t idx) {
  if (idx > 1) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = ZX_OK;
  if (enabled_[idx] || ((status = EnableClock(idx, true)) == ZX_OK)) {
    enabled_[idx] = true;
  }
  return status;
}

zx_status_t AmlPwm::PwmImplDisable(uint32_t idx) {
  if (idx > 1) {
    return ZX_ERR_INVALID_ARGS;
  }
  zx_status_t status = ZX_OK;
  if (!enabled_[idx] || ((status = EnableClock(idx, false)) == ZX_OK)) {
    enabled_[idx] = false;
  }
  return status;
}

zx_status_t AmlPwm::SetMode(uint32_t idx, Mode mode) {
  if (mode >= UNKNOWN) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_en_b(mode == ON || mode == TWO_TIMER)
        .set_ds_en_b(mode == DELTA_SIGMA)
        .set_en_b2(mode == TWO_TIMER);
  } else {
    misc_reg.set_en_a(mode == ON || mode == TWO_TIMER)
        .set_ds_en_a(mode == DELTA_SIGMA)
        .set_en_a2(mode == TWO_TIMER);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetDutyCycle(uint32_t idx, uint32_t period_ns, float duty_cycle) {
  if (duty_cycle > 100 || duty_cycle < 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Write duty cycle to registers
  uint16_t high_count = 0, low_count = 0;
  DutyCycleToClockCount(duty_cycle, period_ns, &high_count, &low_count);
  if (idx % 2) {
    fbl::AutoLock lock(&locks_[REG_B]);
    DutyCycleReg::GetB().ReadFrom(&mmio_).set_high(high_count).set_low(low_count).WriteTo(&mmio_);
  } else {
    fbl::AutoLock lock(&locks_[REG_A]);
    DutyCycleReg::GetA().ReadFrom(&mmio_).set_high(high_count).set_low(low_count).WriteTo(&mmio_);
  }

  return ZX_OK;
}

zx_status_t AmlPwm::SetDutyCycle2(uint32_t idx, uint32_t period_ns, float duty_cycle) {
  if (duty_cycle > 100 || duty_cycle < 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Write duty cycle to registers
  uint16_t high_count = 0, low_count = 0;
  DutyCycleToClockCount(duty_cycle, period_ns, &high_count, &low_count);
  if (idx % 2) {
    fbl::AutoLock lock(&locks_[REG_B2]);
    DutyCycleReg::GetB2().ReadFrom(&mmio_).set_high(high_count).set_low(low_count).WriteTo(&mmio_);
  } else {
    fbl::AutoLock lock(&locks_[REG_A2]);
    DutyCycleReg::GetA2().ReadFrom(&mmio_).set_high(high_count).set_low(low_count).WriteTo(&mmio_);
  }

  return ZX_OK;
}

zx_status_t AmlPwm::Invert(uint32_t idx, bool on) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_inv_en_b(on);
  } else {
    misc_reg.set_inv_en_a(on);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::EnableHiZ(uint32_t idx, bool on) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_hiz_b(on);
  } else {
    misc_reg.set_hiz_a(on);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::EnableClock(uint32_t idx, bool on) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_clk_en_b(on);
  } else {
    misc_reg.set_clk_en_a(on);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::EnableConst(uint32_t idx, bool on) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_constant_en_b(on);
  } else {
    misc_reg.set_constant_en_a(on);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetClock(uint32_t idx, uint8_t sel) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_clk_sel_b(sel);
  } else {
    misc_reg.set_clk_sel_a(sel);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetClockDivider(uint32_t idx, uint8_t div) {
  fbl::AutoLock lock(&locks_[REG_MISC]);
  auto misc_reg = MiscReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    misc_reg.set_clk_div_b(div);
  } else {
    misc_reg.set_clk_div_a(div);
  }
  misc_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::EnableBlink(uint32_t idx, bool on) {
  fbl::AutoLock lock(&locks_[REG_BLINK]);
  auto blink_reg = BlinkReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    blink_reg.set_enable_b(on);
  } else {
    blink_reg.set_enable_a(on);
  }
  blink_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetBlinkTimes(uint32_t idx, uint8_t times) {
  fbl::AutoLock lock(&locks_[REG_BLINK]);
  auto blink_reg = BlinkReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    blink_reg.set_times_b(times);
  } else {
    blink_reg.set_times_a(times);
  }
  blink_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetDSSetting(uint32_t idx, uint16_t val) {
  fbl::AutoLock lock(&locks_[REG_DS]);
  auto ds_reg = DeltaSigmaReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    ds_reg.set_b(val);
  } else {
    ds_reg.set_a(val);
  }
  ds_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwm::SetTimers(uint32_t idx, uint8_t timer1, uint8_t timer2) {
  fbl::AutoLock lock(&locks_[REG_TIME]);
  auto time_reg = TimeReg::Get().ReadFrom(&mmio_);
  if (idx % 2) {
    time_reg.set_b1(timer1).set_b2(timer2);
  } else {
    time_reg.set_a1(timer1).set_a2(timer2);
  }
  time_reg.WriteTo(&mmio_);

  return ZX_OK;
}

zx_status_t AmlPwmDevice::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<AmlPwmDevice> device(new (&ac) AmlPwmDevice(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: device object alloc failed", __func__);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = ZX_OK;
  if ((status = device->Init(parent)) != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed", __func__);
    return status;
  }

  if ((status = device->DdkAdd(
           ddk::DeviceAddArgs("aml-pwm-device").set_proto_id(ZX_PROTOCOL_PWM_IMPL))) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed", __func__);
    return status;
  }

  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t AmlPwmDevice::Init(zx_device_t* parent) {
  zx_status_t status = ZX_OK;

  ddk::PDev pdev(parent);
  for (uint32_t i = 0;; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      break;
    }
    pwms_.push_back(std::make_unique<AmlPwm>(*std::move(mmio)));
    pwms_.back()->Init();
  }

  return ZX_OK;
}

zx_status_t AmlPwmDevice::PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config) {
  if (idx >= pwms_.size() * 2 || out_config == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pwms_[idx / 2]->PwmImplGetConfig(idx % 2, out_config);
}

zx_status_t AmlPwmDevice::PwmImplSetConfig(uint32_t idx, const pwm_config_t* config) {
  if (idx >= pwms_.size() * 2 || config == nullptr || config->mode_config_buffer == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pwms_[idx / 2]->PwmImplSetConfig(idx % 2, config);
}

zx_status_t AmlPwmDevice::PwmImplEnable(uint32_t idx) {
  if (idx >= pwms_.size() * 2) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pwms_[idx / 2]->PwmImplEnable(idx % 2);
}

zx_status_t AmlPwmDevice::PwmImplDisable(uint32_t idx) {
  if (idx >= pwms_.size() * 2) {
    return ZX_ERR_INVALID_ARGS;
  }
  return pwms_[idx / 2]->PwmImplDisable(idx % 2);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlPwmDevice::Create;
  return ops;
}();

}  // namespace pwm

// clang-format off
ZIRCON_DRIVER_BEGIN(pwm, pwm::driver_ops, "zircon", "0.1", 7)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_PWM),
    // we support multiple SOC variants
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_A113),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D3),
ZIRCON_DRIVER_END(pwm)
    // clang-format on
