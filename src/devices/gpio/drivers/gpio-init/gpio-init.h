// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_GPIO_INIT_GPIO_INIT_H_
#define SRC_DEVICES_GPIO_DRIVERS_GPIO_INIT_GPIO_INIT_H_

#include <fidl/fuchsia.hardware.gpio.init/cpp/wire.h>

#include <ddktl/device.h>

namespace gpio_init {

class GpioInit;
using GpioInitDevice = ddk::Device<GpioInit>;

class GpioInit : public GpioInitDevice {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit GpioInit(zx_device_t* parent) : GpioInitDevice(parent) {}

  void DdkRelease() { delete this; }

 private:
  void ConfigureGpios(const fuchsia_hardware_gpio_init::wire::GpioInitMetadata& metadata);
};

}  // namespace gpio_init

#endif  // SRC_DEVICES_GPIO_DRIVERS_GPIO_INIT_GPIO_INIT_H_
