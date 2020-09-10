// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_MT_8167_MT8167_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_MT_8167_MT8167_GPIO_H_

#include <lib/device-protocol/platform-device.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <threads.h>

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/array.h>

#include "mt8167-gpio-regs.h"

namespace gpio {

class Mt8167GpioDevice;
using DeviceType = ddk::Device<Mt8167GpioDevice, ddk::Unbindable>;

class Mt8167GpioDevice : public DeviceType,
                         public ddk::GpioImplProtocol<Mt8167GpioDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit Mt8167GpioDevice(zx_device_t* parent, ddk::MmioBuffer gpio_mmio,
                            ddk::MmioBuffer iocfg_mmio, ddk::MmioBuffer eint_mmio)
      : DeviceType(parent),
        gpio_mmio_(std::move(gpio_mmio)),
        dir_(gpio_mmio_),
        out_(gpio_mmio_),
        in_(gpio_mmio_),
        pull_en_(gpio_mmio_),
        pull_sel_(gpio_mmio_),
        iocfg_(std::move(iocfg_mmio)),
        eint_(std::move(eint_mmio)) {}

  explicit Mt8167GpioDevice(zx_device_t* parent, ddk::MmioBuffer gpio_mmio,
                            ddk::MmioBuffer eint_mmio)
      : DeviceType(parent),
        gpio_mmio_(std::move(gpio_mmio)),
        dir_(gpio_mmio_),
        out_(gpio_mmio_),
        in_(gpio_mmio_),
        pull_en_(gpio_mmio_),
        pull_sel_(gpio_mmio_),
        eint_(std::move(eint_mmio)) {}

  zx_status_t Bind();
  zx_status_t Init();

  // Methods required by the ddk mixins
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint64_t ua, uint64_t* out_actual_ua) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 protected:
  fbl::Array<zx::interrupt> interrupts_;  // Protected to be changed in unit tests.

 private:
  void ShutDown();
  int Thread();

  ddk::MmioBuffer gpio_mmio_;
  const GpioDirReg dir_;
  const GpioOutReg out_;
  const GpioInReg in_;
  const GpioPullEnReg pull_en_;
  const GpioPullSelReg pull_sel_;
  const std::optional<const IoConfigReg> iocfg_;
  const ExtendedInterruptReg eint_;
  zx::interrupt int_;
  zx::port port_;
  thrd_t thread_;
};
}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_MT_8167_MT8167_GPIO_H_
