// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/gpioimpl.h>

namespace gpio {

class GpioDevice;
using GpioDeviceType = ddk::Device<GpioDevice, ddk::UnbindableNew>;

class GpioDevice : public GpioDeviceType, public ddk::GpioProtocol<GpioDevice, ddk::base_protocol> {
 public:
  GpioDevice(zx_device_t* parent, gpio_impl_protocol_t* gpio, uint32_t pin)
      : GpioDeviceType(parent), gpio_(gpio), pin_(pin) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioConfigIn(uint32_t flags);
  zx_status_t GpioConfigOut(uint8_t initial_value);
  zx_status_t GpioSetAltFunction(uint64_t function);
  zx_status_t GpioRead(uint8_t* out_value);
  zx_status_t GpioWrite(uint8_t value);
  zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioReleaseInterrupt();
  zx_status_t GpioSetPolarity(gpio_polarity_t polarity);

 private:
  const ddk::GpioImplProtocolClient gpio_;
  const uint32_t pin_;
};

}  // namespace gpio
