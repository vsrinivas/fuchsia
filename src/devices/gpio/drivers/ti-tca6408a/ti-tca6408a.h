// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_TI_TCA6408A_TI_TCA6408A_H_
#define SRC_DEVICES_GPIO_DRIVERS_TI_TCA6408A_TI_TCA6408A_H_

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/zx/status.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace gpio {

class TiTca6408a;
using DeviceType = ddk::Device<TiTca6408a>;

class TiTca6408aTest;

class TiTca6408a : public DeviceType, public ddk::GpioImplProtocol<TiTca6408a, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  TiTca6408a(zx_device_t* parent, ddk::I2cChannel i2c, uint32_t pin_index_offset)
      : DeviceType(parent), i2c_(i2c), pin_index_offset_(pin_index_offset) {}

  void DdkRelease() { delete this; }

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint64_t ua, uint64_t* out_actual_ua);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity);

 protected:
  friend class TiTca6408aTest;

 private:
  static constexpr uint32_t kPinCount = 8;

  enum class Register : uint8_t {
    kInputPort = 0,
    kOutputPort = 1,
    kPolarityInversion = 2,
    kConfiguration = 3,
  };

  bool IsIndexInRange(uint32_t index) const {
    return index >= pin_index_offset_ && index < (pin_index_offset_ + kPinCount);
  }

  zx::status<uint8_t> ReadBit(Register reg, uint32_t index);
  zx::status<> SetBit(Register reg, uint32_t index);
  zx::status<> ClearBit(Register reg, uint32_t index);

  ddk::I2cChannel i2c_;
  const uint32_t pin_index_offset_;
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_TI_TCA6408A_TI_TCA6408A_H_
