// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_GPIO_DRIVERS_AML_GXL_GPIO_AML_GXL_GPIO_H_
#define SRC_DEVICES_GPIO_DRIVERS_AML_GXL_GPIO_AML_GXL_GPIO_H_

#include <inttypes.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <utility>

#include <ddk/protocol/platform/bus.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <hw/reg.h>

namespace gpio {

constexpr int kPinsPerBlock = 32;
constexpr uint64_t kAltFunctionMax = 6;

struct AmlGpioBlock {
  uint32_t pin_count;
  uint32_t oen_offset;
  uint32_t input_offset;
  uint32_t output_offset;
  uint32_t output_shift;  // Used for GPIOAO block
  uint32_t output_write_shift = 0;
  uint32_t mmio_index;
  uint32_t pull_offset;
  uint32_t pull_en_offset;
  uint32_t pin_start;
};

struct AmlPinMux {
  // pinmux register offsets for the alternate functions.
  // zero means alternate function not supported.
  uint8_t regs[kAltFunctionMax];
  // bit number to set/clear to enable/disable alternate function
  uint8_t bits[kAltFunctionMax];
};

struct AmlPinMuxBlock {
  AmlPinMux mux[kPinsPerBlock];
};

struct AmlGpioInterrupt {
  uint32_t pin_select_offset;
  uint32_t edge_polarity_offset;
  uint32_t filter_select_offset;
  uint32_t status_offset;
  uint32_t mask_offset;
};

class AmlGxlGpio;
using DeviceType = ddk::Device<AmlGxlGpio, ddk::Unbindable>;

class AmlGxlGpio : public DeviceType, public ddk::GpioImplProtocol<AmlGxlGpio, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
  zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
  zx_status_t GpioImplSetAltFunction(uint32_t pin, uint64_t function);
  zx_status_t GpioImplRead(uint32_t pin, uint8_t* out_value);
  zx_status_t GpioImplWrite(uint32_t pin, uint8_t value);
  zx_status_t GpioImplGetInterrupt(uint32_t pin, uint32_t flags, zx::interrupt* out_irq);
  zx_status_t GpioImplReleaseInterrupt(uint32_t pin);
  zx_status_t GpioImplSetPolarity(uint32_t pin, uint32_t polarity);
  zx_status_t GpioImplSetDriveStrength(uint32_t index, uint64_t ua, uint64_t* out_actual_ua) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

 private:
  AmlGxlGpio(zx_device_t* parent, const pdev_protocol_t& pdev, ddk::MmioBuffer mmio_gpio,
             ddk::MmioBuffer mmio_gpio_a0, ddk::MmioBuffer mmio_interrupt,
             const AmlGpioBlock* gpio_blocks, const AmlGpioInterrupt* gpio_interrupt,
             const AmlPinMuxBlock* pinmux_blocks, size_t block_count, fbl::Array<uint16_t> irq_info)
      : DeviceType(parent),
        pdev_(pdev),
        mmios_{std::move(mmio_gpio), std::move(mmio_gpio_a0)},
        mmio_interrupt_(std::move(mmio_interrupt)),
        gpio_blocks_(gpio_blocks),
        gpio_interrupt_(gpio_interrupt),
        pinmux_blocks_(pinmux_blocks),
        block_count_(block_count),
        irq_info_(std::move(irq_info)),
        irq_status_(0) {}

  // Note: The out_pin_index returned by this API is not the index of the pin
  // in the particular GPIO block. eg. if its 7, its not GPIOH7
  // It is the index of the bit corresponding to the GPIO in consideration in a
  // particular INPUT/OUTPUT/PULL-UP/PULL-DOWN/PULL-ENABLE/ENABLE register
  // out_block and out_lock are owned by the AmlGxlGpio instance and should not be deleted by
  // callers.
  zx_status_t AmlPinToBlock(const uint32_t pin, const AmlGpioBlock** out_block,
                            uint32_t* out_pin_index) const;

  void Bind(const pbus_protocol_t& pbus);

  uint32_t Read32GpioReg(int index, uint32_t offset) TA_REQ(mmio_lock_) {
    return mmios_[index].Read32(offset << 2);
  }

  void Write32GpioReg(int index, uint32_t offset, uint32_t value) TA_REQ(mmio_lock_) {
    mmios_[index].Write32(value, offset << 2);
  }

  uint32_t Read32GpioInterruptReg(uint32_t offset) TA_REQ(mmio_lock_) {
    return mmio_interrupt_.Read32(offset << 2);
  }

  void Write32GpioInterruptReg(uint32_t offset, uint32_t value) TA_REQ(mmio_lock_) {
    mmio_interrupt_.Write32(value, offset << 2);
  }

  pdev_protocol_t pdev_;
  fbl::Mutex mmio_lock_;
  ddk::MmioBuffer mmios_[2] TA_GUARDED(mmio_lock_);  // separate MMIO for AO domain
  ddk::MmioBuffer mmio_interrupt_ TA_GUARDED(mmio_lock_);
  const AmlGpioBlock* gpio_blocks_;
  const AmlGpioInterrupt* gpio_interrupt_;
  const AmlPinMuxBlock* pinmux_blocks_;
  size_t block_count_;
  fbl::Mutex irq_lock_ TA_ACQ_BEFORE(mmio_lock_);
  fbl::Array<uint16_t> irq_info_ TA_GUARDED(irq_lock_);
  uint8_t irq_status_ TA_GUARDED(irq_lock_);
};

}  // namespace gpio

#endif  // SRC_DEVICES_GPIO_DRIVERS_AML_GXL_GPIO_AML_GXL_GPIO_H_
