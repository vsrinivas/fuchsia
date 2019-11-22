// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_PWM_PWM_PWM_H_
#define ZIRCON_SYSTEM_DEV_PWM_PWM_PWM_H_

#include <ddk/metadata/pwm.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/pwm.h>

namespace pwm {

class PwmDevice;
using PwmDeviceType = ddk::Device<PwmDevice, ddk::UnbindableNew>;

class PwmDevice : public PwmDeviceType, public ddk::PwmProtocol<PwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  // Ddk Mixins.
  zx_status_t PwmGetConfig(pwm_config_t* out_config);
  zx_status_t PwmSetConfig(const pwm_config_t* config);
  zx_status_t PwmEnable();
  zx_status_t PwmDisable();

 protected:
  // For unit testing
  explicit PwmDevice(pwm_impl_protocol_t* pwm) : PwmDeviceType(nullptr), pwm_(pwm), id_({0}) {}

 private:
  explicit PwmDevice(zx_device_t* parent, pwm_impl_protocol_t* pwm, pwm_id_t id)
      : PwmDeviceType(parent), pwm_(pwm), id_(id) {}

  ddk::PwmImplProtocolClient pwm_;
  pwm_id_t id_;
};

}  // namespace pwm

#endif  // ZIRCON_SYSTEM_DEV_PWM_PWM_PWM_H_
