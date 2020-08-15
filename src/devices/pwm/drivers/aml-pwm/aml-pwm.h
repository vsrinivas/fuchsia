// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PWM_DRIVERS_AML_PWM_AML_PWM_H_
#define SRC_DEVICES_PWM_DRIVERS_AML_PWM_AML_PWM_H_

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <array>
#include <vector>

#include <ddk/metadata/pwm.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pwm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "aml-pwm-regs.h"

namespace pwm {

class AmlPwm;
class AmlPwmDevice;
using AmlPwmDeviceType = ddk::Device<AmlPwmDevice, ddk::UnbindableNew>;

class AmlPwm {
 public:
  explicit AmlPwm(ddk::MmioBuffer mmio, pwm_id_t id1, pwm_id_t id2)
      : ids_{id1, id2}, enabled_{false, false}, mmio_(std::move(mmio)) {}

  void Init() {
    if (!ids_[0].protect) {
      mode_configs_[0].mode = OFF;
      mode_configs_[0].regular = {};
      configs_[0] = {false, 0, 0.0, &mode_configs_[0], sizeof(mode_config)};
      SetMode(0, OFF);
    }
    if (!ids_[1].protect) {
      mode_configs_[1].mode = OFF;
      mode_configs_[1].regular = {};
      configs_[1] = {false, 0, 0.0, &mode_configs_[1], sizeof(mode_config)};
      SetMode(1, OFF);
    }
  }

  zx_status_t PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config);
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config);
  zx_status_t PwmImplEnable(uint32_t idx);
  zx_status_t PwmImplDisable(uint32_t idx);

 private:
  friend class AmlPwmDevice;

  // Register fine control.
  zx_status_t SetMode(uint32_t idx, Mode mode);
  zx_status_t SetDutyCycle(uint32_t idx, uint32_t period, float duty_cycle);
  zx_status_t SetDutyCycle2(uint32_t idx, uint32_t period, float duty_cycle);
  zx_status_t Invert(uint32_t idx, bool on);
  zx_status_t EnableHiZ(uint32_t idx, bool on);
  zx_status_t EnableClock(uint32_t idx, bool on);
  zx_status_t EnableConst(uint32_t idx, bool on);
  zx_status_t SetClock(uint32_t idx, uint8_t sel);
  zx_status_t SetClockDivider(uint32_t idx, uint8_t div);
  zx_status_t EnableBlink(uint32_t idx, bool on);
  zx_status_t SetBlinkTimes(uint32_t idx, uint8_t times);
  zx_status_t SetDSSetting(uint32_t idx, uint16_t val);
  zx_status_t SetTimers(uint32_t idx, uint8_t timer1, uint8_t timer2);

  std::array<pwm_id_t, 2> ids_;
  std::array<bool, 2> enabled_;
  std::array<pwm_config_t, 2> configs_;
  std::array<mode_config, 2> mode_configs_;
  std::array<fbl::Mutex, REG_COUNT> locks_;
  ddk::MmioBuffer mmio_;
};

class AmlPwmDevice : public AmlPwmDeviceType,
                     public ddk::PwmImplProtocol<AmlPwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  zx_status_t PwmImplGetConfig(uint32_t idx, pwm_config_t* out_config);
  zx_status_t PwmImplSetConfig(uint32_t idx, const pwm_config_t* config);
  zx_status_t PwmImplEnable(uint32_t idx);
  zx_status_t PwmImplDisable(uint32_t idx);

 protected:
  // For unit testing
  explicit AmlPwmDevice() : AmlPwmDeviceType(nullptr) {}
  zx_status_t Init(ddk::MmioBuffer mmio0, ddk::MmioBuffer mmio1, ddk::MmioBuffer mmio2,
                   ddk::MmioBuffer mmio3, ddk::MmioBuffer mmio4, std::vector<pwm_id_t> ids) {
    pwms_.push_back(std::make_unique<AmlPwm>(std::move(mmio0), ids.at(0), ids.at(1)));
    pwms_.back()->Init();
    pwms_.push_back(std::make_unique<AmlPwm>(std::move(mmio1), ids.at(2), ids.at(3)));
    pwms_.back()->Init();
    pwms_.push_back(std::make_unique<AmlPwm>(std::move(mmio2), ids.at(4), ids.at(5)));
    pwms_.back()->Init();
    pwms_.push_back(std::make_unique<AmlPwm>(std::move(mmio3), ids.at(6), ids.at(7)));
    pwms_.back()->Init();
    pwms_.push_back(std::make_unique<AmlPwm>(std::move(mmio4), ids.at(8), ids.at(9)));
    pwms_.back()->Init();

    return ZX_OK;
  }

 private:
  explicit AmlPwmDevice(zx_device_t* parent) : AmlPwmDeviceType(parent) {}

  zx_status_t Init(zx_device_t* parent);

  std::vector<std::unique_ptr<AmlPwm>> pwms_;
};

}  // namespace pwm

#endif  // SRC_DEVICES_PWM_DRIVERS_AML_PWM_AML_PWM_H_
