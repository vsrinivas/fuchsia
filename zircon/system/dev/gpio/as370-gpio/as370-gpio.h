// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_GPIO_AS370_GPIO_AS370_GPIO_H_
#define ZIRCON_SYSTEM_DEV_GPIO_AS370_GPIO_AS370_GPIO_H_

#include <lib/mmio/mmio.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/array.h>

namespace gpio {

class As370Gpio : public ddk::Device<As370Gpio, ddk::UnbindableNew>,
                  public ddk::GpioImplProtocol<As370Gpio, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  As370Gpio(zx_device_t* parent, ddk::MmioBuffer pinmux_mmio, ddk::MmioBuffer gpio1_mmio,
            ddk::MmioBuffer gpio2_mmio, zx::interrupt gpio1_irq)
      : ddk::Device<As370Gpio, ddk::UnbindableNew>(parent),
        pinmux_mmio_(std::move(pinmux_mmio)),
        gpio1_mmio_(std::move(gpio1_mmio)),
        gpio2_mmio_(std::move(gpio2_mmio)),
        gpio1_irq_(std::move(gpio1_irq)) {}
  virtual ~As370Gpio() = default;

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint8_t m_a);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  virtual zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity);

  zx_status_t Init();
  void Shutdown();

 protected:
  ddk::MmioBuffer pinmux_mmio_;
  ddk::MmioBuffer gpio1_mmio_;
  ddk::MmioBuffer gpio2_mmio_;

 private:
  zx_status_t Bind();
  int Thread();
  inline void SetInterruptPolarity(uint32_t index, bool is_high);
  inline void SetInterruptEdge(uint32_t index, bool is_edge);
  inline bool IsInterruptEnabled(uint32_t index);

  thrd_t thread_;
  zx::interrupt gpio1_irq_;
  fbl::Array<zx::interrupt> interrupts_;
  zx::port port_;
};

}  // namespace gpio

#endif  // ZIRCON_SYSTEM_DEV_GPIO_AS370_GPIO_AS370_GPIO_H_
