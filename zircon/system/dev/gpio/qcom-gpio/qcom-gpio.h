// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_GPIO_QCOM_GPIO_QCOM_GPIO_H_
#define ZIRCON_SYSTEM_DEV_GPIO_QCOM_GPIO_QCOM_GPIO_H_

#include <threads.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/array.h>
#include <hwreg/bitfields.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <zircon/types.h>

#include "qcom-gpio-regs.h"

namespace gpio {

class QcomGpioDevice;
using DeviceType = ddk::Device<QcomGpioDevice, ddk::UnbindableNew>;

class QcomGpioDevice : public DeviceType,
                       public ddk::GpioImplProtocol<QcomGpioDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit QcomGpioDevice(zx_device_t* parent, ddk::MmioBuffer gpio_mmio)
      : DeviceType(parent),
        gpio_mmio_(std::move(gpio_mmio)),
        in_out_(gpio_mmio_),
        int_cfg_(gpio_mmio_),
        dir_conn_int_(gpio_mmio_),
        status_int_(gpio_mmio_),
        pdev_(parent) {}

  zx_status_t Bind();
  zx_status_t Init();

  // Methods required by the ddk mixins
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint8_t mA);

 protected:
  // Protected to be accessed by unit tests.
  fbl::Array<zx::interrupt> interrupts_;

 private:
  void ShutDown();
  int Thread();

  ddk::MmioBuffer gpio_mmio_;
  const GpioInOutReg in_out_;
  const GpioIntCfgReg int_cfg_;
  const TlmmDirConnIntReg dir_conn_int_;
  const TlmmGpioIntrStatusReg status_int_;
  zx::port port_;
  thrd_t thread_;
  ddk::PDev pdev_;
  // Cache for faster traversal finding triggered interrupts.
  bitmap::RawBitmapGeneric<bitmap::DefaultStorage> enabled_ints_cache_;
  zx::interrupt combined_int_;
};
}  // namespace gpio

#endif  // ZIRCON_SYSTEM_DEV_GPIO_QCOM_GPIO_QCOM_GPIO_H_
