// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_GPIO_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_GPIO_GPIO_H_

#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <fuchsia/hardware/gpio/llcpp/fidl.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/fidl.h>
#include <fbl/mutex.h>

namespace gpio {

class GpioDevice;
using GpioDeviceType = ddk::Device<GpioDevice, ddk::Unbindable, ddk::Messageable>;
using fuchsia_hardware_gpio::Gpio;
using fuchsia_hardware_gpio::wire::GpioFlags;

static_assert(GPIO_PULL_DOWN == static_cast<uint32_t>(GpioFlags::PULL_DOWN),
              "ConfigIn PULL_DOWN flag doesn't match.");
static_assert(GPIO_PULL_UP == static_cast<uint32_t>(GpioFlags::PULL_UP),
              "ConfigIn PULL_UP flag doesn't match.");
static_assert(GPIO_NO_PULL == static_cast<uint32_t>(GpioFlags::NO_PULL),
              "ConfigIn NO_PULL flag doesn't match.");
static_assert(GPIO_PULL_MASK == static_cast<uint32_t>(GpioFlags::PULL_MASK),
              "ConfigIn PULL_MASK flag doesn't match.");

class GpioDevice : public GpioDeviceType,
                   public Gpio::Interface,
                   public ddk::GpioProtocol<GpioDevice, ddk::base_protocol> {
 public:
  GpioDevice(zx_device_t* parent, gpio_impl_protocol_t* gpio, uint32_t pin)
      : GpioDeviceType(parent), gpio_(gpio), pin_(pin) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t DdkMessage(fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fidl::WireDispatch<Gpio>(this, msg, &transaction);
    return transaction.Status();
  }
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioConfigIn(uint32_t flags);
  zx_status_t GpioConfigOut(uint8_t initial_value);
  zx_status_t GpioSetAltFunction(uint64_t function);
  zx_status_t GpioRead(uint8_t* out_value);
  zx_status_t GpioWrite(uint8_t value);
  zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioReleaseInterrupt();
  zx_status_t GpioSetPolarity(gpio_polarity_t polarity);
  zx_status_t GpioSetDriveStrength(uint64_t ds_ua, uint64_t* out_actual_ds_ua);

  // FIDL
  void ConfigIn(GpioFlags flags, ConfigInCompleter::Sync& completer) {
    zx_status_t status = GpioConfigIn(static_cast<uint32_t>(flags));
    if (status == ZX_OK) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(status);
    }
  }
  void ConfigOut(uint8_t initial_value, ConfigOutCompleter::Sync& completer) {
    zx_status_t status = GpioConfigOut(initial_value);
    if (status == ZX_OK) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(status);
    }
  }
  void Read(ReadCompleter::Sync& completer) {
    uint8_t value = 0;
    zx_status_t status = GpioRead(&value);
    if (status == ZX_OK) {
      completer.ReplySuccess(value);
    } else {
      completer.ReplyError(status);
    }
  }
  void Write(uint8_t value, WriteCompleter::Sync& completer) {
    zx_status_t status = GpioWrite(value);
    if (status == ZX_OK) {
      completer.ReplySuccess();
    } else {
      completer.ReplyError(status);
    }
  }
  void SetDriveStrength(uint64_t ds_ua, SetDriveStrengthCompleter::Sync& completer) {
    uint64_t actual = 0;
    zx_status_t status = GpioSetDriveStrength(ds_ua, &actual);
    if (status == ZX_OK) {
      completer.ReplySuccess(actual);
    } else {
      completer.ReplyError(status);
    }
  }

 private:
  const ddk::GpioImplProtocolClient gpio_ TA_GUARDED(lock_);
  const uint32_t pin_;
  fbl::Mutex lock_;
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_GPIO_GPIO_H_
