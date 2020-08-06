// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_BCM2711_GPIO_BCM2711_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_BCM2711_GPIO_BCM2711_GPIO_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddk/protocol/platform/bus.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>

namespace gpio {

class Bcm2711Gpio;
using DeviceType = ddk::Device<Bcm2711Gpio, ddk::UnbindableNew>;

class Bcm2711Gpio : public DeviceType, public ddk::GpioImplProtocol<Bcm2711Gpio, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint8_t m_a) { return ZX_ERR_NOT_SUPPORTED; }

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }
  void Shutdown();

 private:
  explicit Bcm2711Gpio(zx_device_t* parent, ddk::MmioBuffer mmio_gpio,
                       fbl::Array<zx::interrupt> port_interrupts)
      : DeviceType(parent),
        pdev_(parent),
        mmio_(std::move(mmio_gpio)),
        port_interrupts_(std::move(port_interrupts)) {}

  zx_status_t Init();
  void Bind(const pbus_protocol_t& pbus);
  int Thread();

  ddk::PDev pdev_;
  fbl::Mutex mmio_lock_;
  ddk::MmioBuffer mmio_ TA_GUARDED(mmio_lock_);
  fbl::Mutex irq_lock_ TA_ACQ_BEFORE(mmio_lock_);  
  fbl::Array<zx::interrupt> port_interrupts_ TA_GUARDED(irq_lock_);
  fbl::Array<zx::interrupt> gpio_interrupts_ TA_GUARDED(irq_lock_);
  zx::port port_;
  thrd_t thread_;
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_BCM2711_GPIO_BCM2711_GPIO_H_
