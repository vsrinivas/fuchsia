// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_PWM_DRIVERS_AML_PWM_INIT_AML_PWM_INIT_H_
#define SRC_DEVICES_PWM_DRIVERS_AML_PWM_INIT_AML_PWM_INIT_H_

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/pwm.h>
#include <soc/aml-common/aml-pwm-regs.h>

namespace pwm_init {

class PwmInitDevice;
using PwmInitDeviceType = ddk::Device<PwmInitDevice, ddk::UnbindableNew>;

class PwmInitDevice : public PwmInitDeviceType {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  friend class FakePwmInitDevice;

  enum {
    COMPONENT_PWM,
    COMPONENT_WIFI_GPIO,
    COMPONENT_BT_GPIO,
    COMPONENT_COUNT,
  };

  explicit PwmInitDevice(zx_device_t* parent, ddk::PwmProtocolClient pwm,
                         ddk::GpioProtocolClient wifi_gpio, ddk::GpioProtocolClient bt_gpio)
      : PwmInitDeviceType(parent), pwm_(pwm), wifi_gpio_(wifi_gpio), bt_gpio_(bt_gpio) {}

  zx_status_t Init();

  ddk::PwmProtocolClient pwm_;
  ddk::GpioProtocolClient wifi_gpio_;
  ddk::GpioProtocolClient bt_gpio_;
};

}  // namespace pwm_init

#endif  // SRC_DEVICES_PWM_DRIVERS_AML_PWM_INIT_AML_PWM_INIT_H_
