// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_AML_AXG_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_AML_AXG_GPIO_H_

#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <array>
#include <cstdint>

#include <ddk/protocol/platform/bus.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

namespace gpio {

struct AmlGpioBlock {
  fbl::Mutex block_lock;  // Protects the regions of the MMIO determined by the fields below

  uint32_t start_pin;
  uint32_t pin_block;
  uint32_t pin_count;
  uint32_t mux_offset;
  uint32_t oen_offset;
  uint32_t input_offset;
  uint32_t output_offset;
  uint32_t output_shift;  // Used for GPIOAO block
  uint32_t pull_offset;
  uint32_t pull_en_offset;
  uint32_t mmio_index;
  uint32_t pin_start;
  uint32_t ds_offset;
};

struct AmlGpioInterrupt {
  fbl::Mutex interrupt_lock;  // Protects content in fields below
                              // and info_, irq_info_, irq_status_

  uint32_t pin_0_3_select_offset;
  uint32_t pin_4_7_select_offset;
  uint32_t edge_polarity_offset;
  uint32_t filter_select_offset;
};

class AmlAxgGpio;
using DeviceType = ddk::Device<AmlAxgGpio, ddk::UnbindableNew>;

class AmlAxgGpio : public DeviceType, public ddk::GpioImplProtocol<AmlAxgGpio, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint8_t m_a);
  zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t index);
  zx_status_t GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 protected:
  // for AmlAxgGpioTest
  explicit AmlAxgGpio(pdev_protocol_t* proto, ddk::MmioBuffer mmio_gpio,
                      ddk::MmioBuffer mmio_gpio_a0, ddk::MmioBuffer mmio_interrupt,
                      AmlGpioBlock* gpio_blocks, AmlGpioInterrupt* gpio_interrupt,
                      size_t block_count, pdev_device_info_t info, fbl::Array<uint16_t> irq_info)
      : DeviceType(nullptr),
        pdev_(proto),
        mmios_{std::move(mmio_gpio), std::move(mmio_gpio_a0)},
        mmio_interrupt_(std::move(mmio_interrupt)),
        gpio_blocks_(gpio_blocks),
        gpio_interrupt_(gpio_interrupt),
        block_count_(block_count),
        info_(std::move(info)),
        irq_info_(std::move(irq_info)),
        irq_status_(0) {}

 private:
  explicit AmlAxgGpio(zx_device_t* parent, ddk::MmioBuffer mmio_gpio, ddk::MmioBuffer mmio_gpio_a0,
                      ddk::MmioBuffer mmio_interrupt, AmlGpioBlock* gpio_blocks,
                      AmlGpioInterrupt* gpio_interrupt, size_t block_count, pdev_device_info_t info,
                      fbl::Array<uint16_t> irq_info)
      : DeviceType(parent),
        pdev_(parent),
        mmios_{std::move(mmio_gpio), std::move(mmio_gpio_a0)},
        mmio_interrupt_(std::move(mmio_interrupt)),
        gpio_blocks_(gpio_blocks),
        gpio_interrupt_(gpio_interrupt),
        block_count_(block_count),
        info_(std::move(info)),
        irq_info_(std::move(irq_info)),
        irq_status_(0) {}

  zx_status_t AmlPinToBlock(uint32_t pin, AmlGpioBlock** out_block, uint32_t* out_pin_index) const;

  void Bind(const pbus_protocol_t& pbus);

  ddk::PDev pdev_;
  std::array<ddk::MmioBuffer, 2> mmios_;  // separate MMIO for AO domain
  ddk::MmioBuffer mmio_interrupt_;
  AmlGpioBlock* gpio_blocks_;
  AmlGpioInterrupt* gpio_interrupt_;
  size_t block_count_;
  pdev_device_info_t info_ TA_GUARDED(gpio_interrupt_->interrupt_lock);
  fbl::Array<uint16_t> irq_info_ TA_GUARDED(gpio_interrupt_->interrupt_lock);
  uint8_t irq_status_ TA_GUARDED(gpio_interrupt_->interrupt_lock);
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_AML_AXG_GPIO_AML_AXG_GPIO_H_
