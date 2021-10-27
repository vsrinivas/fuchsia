// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PWM_DRIVERS_PWM_PWM_H_
#define SRC_DEVICES_PWM_DRIVERS_PWM_PWM_H_

#include <fidl/fuchsia.hardware.pwm/cpp/wire.h>
#include <fuchsia/hardware/pwm/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/pwm.h>
#include <ddktl/device.h>

namespace pwm {

class PwmDevice;
using PwmDeviceType = ddk::Device<PwmDevice, ddk::Messageable<fuchsia_hardware_pwm::Pwm>::Mixin>;

class PwmDevice : public PwmDeviceType, public ddk::PwmProtocol<PwmDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  // Ddk Mixins.
  zx_status_t PwmGetConfig(pwm_config_t* out_config);
  zx_status_t PwmSetConfig(const pwm_config_t* config);
  zx_status_t PwmEnable();
  zx_status_t PwmDisable();

  void GetConfig(GetConfigRequestView request, GetConfigCompleter::Sync& completer) override;
  void SetConfig(SetConfigRequestView request, SetConfigCompleter::Sync& completer) override;
  void Enable(EnableRequestView request, EnableCompleter::Sync& completer) override;
  void Disable(DisableRequestView request, DisableCompleter::Sync& completer) override;

 private:
  explicit PwmDevice(zx_device_t* parent, pwm_impl_protocol_t* pwm, pwm_id_t id)
      : PwmDeviceType(parent), pwm_(pwm), id_(id) {}

  ddk::PwmImplProtocolClient pwm_;
  pwm_id_t id_;
};

}  // namespace pwm

#endif  // SRC_DEVICES_PWM_DRIVERS_PWM_PWM_H_
