// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_H_

#include <lib/mmio/mmio.h>
#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <soc/synaptics/gpio.h>

namespace gpio {

class As370Gpio : public ddk::Device<As370Gpio, ddk::Unbindable>,
                  public ddk::GpioImplProtocol<As370Gpio, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  As370Gpio(zx_device_t* parent, fbl::Vector<ddk::MmioBuffer> pinmux_mmios,
            fbl::Vector<ddk::MmioBuffer> gpio_mmios, fbl::Array<zx::interrupt> port_interrupts,
            const synaptics::PinmuxMetadata& pinmux_metadata)
      : ddk::Device<As370Gpio, ddk::Unbindable>(parent),
        pinmux_mmios_(std::move(pinmux_mmios)),
        gpio_mmios_(std::move(gpio_mmios)),
        port_interrupts_(std::move(port_interrupts)),
        pinmux_metadata_(pinmux_metadata) {}
  virtual ~As370Gpio() = default;

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint64_t ua, uint64_t* out_actual_ua);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity);

  zx_status_t Init();
  void Shutdown();

 protected:
  fbl::Vector<ddk::MmioBuffer> pinmux_mmios_;
  fbl::Vector<ddk::MmioBuffer> gpio_mmios_;

 private:
  zx_status_t Bind();
  int Thread();
  inline void SetInterruptPolarity(uint32_t index, bool is_high);
  inline void SetInterruptEdge(uint32_t index, bool is_edge);
  inline bool IsInterruptEnabled(uint64_t index);

  thrd_t thread_;
  fbl::Array<zx::interrupt> port_interrupts_;
  fbl::Array<zx::interrupt> gpio_interrupts_;
  zx::port port_;
  const synaptics::PinmuxMetadata pinmux_metadata_;
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_AS370_GPIO_AS370_GPIO_H_
