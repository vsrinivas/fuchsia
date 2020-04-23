// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-gxl-gpio.h"

#include <lib/device-protocol/platform-device.h>

#include <memory>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include "s912-blocks.h"

namespace {

constexpr int kGpioInterruptPolarityShift = 16;
constexpr int kMaxGpioIndex = 255;
constexpr int kBitsPerGpioInterrupt = 8;
constexpr int kBitsPerFilterSelect = 4;

uint32_t GetUnusedIrqIndex(uint8_t status) {
  // First isolate the rightmost 0-bit
  uint8_t zero_bit_set = static_cast<uint8_t>(~status & (status + 1));
  // Count no. of leading zeros
  return __builtin_ctz(zero_bit_set);
}

}  // namespace

namespace gpio {

// MMIO indices (based on vim-gpio.c gpio_mmios)
enum {
  MMIO_GPIO = 0,
  MMIO_GPIO_A0 = 1,
  MMIO_GPIO_INTERRUPTS = 2,
};

zx_status_t AmlGxlGpio::Create(zx_device_t* parent) {
  zx_status_t status;

  pdev_protocol_t pdev;
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: ZX_PROTOCOL_PDEV not available");
    return status;
  }

  pbus_protocol_t pbus;
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: ZX_PROTOCOL_PBUS not available");
    return status;
  }

  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev, MMIO_GPIO, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: pdev_map_mmio_buffer failed");
    return status;
  }

  ddk::MmioBuffer mmio_gpio(mmio);

  status = pdev_map_mmio_buffer(&pdev, MMIO_GPIO_A0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: pdev_map_mmio_buffer failed");
    return status;
  }

  ddk::MmioBuffer mmio_gpio_a0(mmio);

  status =
      pdev_map_mmio_buffer(&pdev, MMIO_GPIO_INTERRUPTS, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: pdev_map_mmio_buffer failed");
    return status;
  }

  ddk::MmioBuffer mmio_interrupt(mmio);

  pdev_device_info_t info;
  if ((status = pdev_get_device_info(&pdev, &info)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: pdev_get_device_info failed");
    return status;
  }

  const AmlGpioBlock* gpio_blocks;
  const AmlPinMuxBlock* pinmux_blocks;
  const AmlGpioInterrupt* gpio_interrupt;
  size_t block_count;

  switch (info.pid) {
    case PDEV_PID_AMLOGIC_S912:
      gpio_blocks = s912_gpio_blocks;
      pinmux_blocks = s912_pinmux_blocks;
      gpio_interrupt = &s912_interrupt_block;
      block_count = countof(s912_gpio_blocks);
      break;
    default:
      zxlogf(ERROR, "AmlGxlGpio::Create: unsupported SOC PID %u", info.pid);
      return ZX_ERR_INVALID_ARGS;
  }

  fbl::AllocChecker ac;

  fbl::Array<uint16_t> irq_info(new (&ac) uint16_t[info.irq_count], info.irq_count);
  if (!ac.check()) {
    zxlogf(ERROR, "AmlGxlGpio::Create: irq_info alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  fbl::Array<fbl::Mutex> block_locks(new (&ac) fbl::Mutex[block_count], block_count);
  if (!ac.check()) {
    zxlogf(ERROR, "AmlGxlGpio::Create: block locks alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  std::unique_ptr<AmlGxlGpio> device(
      new (&ac) AmlGxlGpio(parent, pdev, std::move(mmio_gpio), std::move(mmio_gpio_a0),
                           std::move(mmio_interrupt), gpio_blocks, gpio_interrupt, pinmux_blocks,
                           block_count, std::move(block_locks), std::move(irq_info)));
  if (!ac.check()) {
    zxlogf(ERROR, "AmlGxlGpio::Create: device object alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  device->Bind(pbus);

  if ((status = device->DdkAdd("aml-gxl-gpio")) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::Create: DdkAdd failed");
    return status;
  }

  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

void AmlGxlGpio::Bind(const pbus_protocol_t& pbus) {
  gpio_impl_protocol_t gpio_proto = {
      .ops = &gpio_impl_protocol_ops_,
      .ctx = this,
  };

  pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
}

zx_status_t AmlGxlGpio::AmlPinToBlock(const uint32_t pin, const AmlGpioBlock** out_block,
                                      uint32_t* out_pin_index, fbl::Mutex** out_lock) const {
  ZX_DEBUG_ASSERT(out_block && out_pin_index);

  uint32_t block_index = pin / kPinsPerBlock;
  if (block_index >= block_count_) {
    return ZX_ERR_NOT_FOUND;
  }
  const AmlGpioBlock* block = &gpio_blocks_[block_index];
  uint32_t pin_index = pin % kPinsPerBlock;
  if (pin_index >= block->pin_count) {
    return ZX_ERR_NOT_FOUND;
  }
  pin_index += block->output_shift;
  *out_block = block;
  *out_pin_index = pin_index;
  *out_lock = &block_locks_[block_index];
  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  zx_status_t status;

  const AmlGpioBlock* block;
  uint32_t pin_index;
  fbl::Mutex* block_lock;
  if ((status = AmlPinToBlock(index, &block, &pin_index, &block_lock)) != ZX_OK) {
    zxlogf(ERROR, "AmGxlGpio::GpioImplConfigIn: pin not found %u", index);
    return status;
  }

  // Set the GPIO as IN or OUT
  fbl::AutoLock al(block_lock);
  uint32_t regval = Read32GpioReg(block->mmio_index, block->oen_offset);
  // Set the GPIO as pull-up or pull-down
  uint32_t pull_reg_val = Read32GpioReg(block->mmio_index, block->pull_offset);
  uint32_t pull_en_reg_val = Read32GpioReg(block->mmio_index, block->pull_en_offset);
  uint32_t pull_pin_index = pin_index;
  if (block->output_write_shift) {
    // Handling special case where output_offset is
    // different for OEN/OUT/PU-PD for GPIOA0 block
    pull_pin_index += block->output_write_shift;
  }
  if (flags & GPIO_NO_PULL) {
    pull_en_reg_val &= ~(1 << pin_index);
  } else {
    if (flags & GPIO_PULL_UP) {
      pull_reg_val |= (1 << pull_pin_index);
    } else {
      pull_reg_val &= ~(1 << pull_pin_index);
    }
    pull_en_reg_val |= (1 << pin_index);
  }
  Write32GpioReg(block->mmio_index, block->pull_offset, pull_reg_val);
  Write32GpioReg(block->mmio_index, block->pull_en_offset, pull_en_reg_val);
  regval |= (1 << pin_index);
  Write32GpioReg(block->mmio_index, block->oen_offset, regval);

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  zx_status_t status;

  const AmlGpioBlock* block;
  uint32_t pin_index;
  fbl::Mutex* block_lock;
  if ((status = AmlPinToBlock(index, &block, &pin_index, &block_lock)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::GpioImplConfigOut: pin not found %u", index);
    return status;
  }

  fbl::AutoLock al(block_lock);

  // Set value before configuring for output
  uint32_t regval = Read32GpioReg(block->mmio_index, block->output_offset);
  // output_write_shift is handling special case where output_offset is
  // different for OEN/OUT for GPIOA0 block
  if (initial_value) {
    regval |= (1 << (pin_index + block->output_write_shift));
  } else {
    regval &= ~(1 << (pin_index + block->output_write_shift));
  }
  Write32GpioReg(block->mmio_index, block->output_offset, regval);

  regval = Read32GpioReg(block->mmio_index, block->oen_offset);
  regval &= ~(1 << pin_index);
  Write32GpioReg(block->mmio_index, block->oen_offset, regval);

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplSetAltFunction(uint32_t pin, uint64_t function) {
  if (function > kAltFunctionMax) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t block_index = pin / kPinsPerBlock;
  if (block_index >= block_count_) {
    return ZX_ERR_NOT_FOUND;
  }
  const AmlPinMuxBlock* block = &pinmux_blocks_[block_index];
  uint32_t pin_index = pin % kPinsPerBlock;
  const AmlPinMux* mux = &block->mux[pin_index];

  const AmlGpioBlock* gpio_block = &gpio_blocks_[block_index];
  fbl::AutoLock al(&pinmux_lock_);

  for (uint64_t i = 0; i < kAltFunctionMax; i++) {
    uint32_t reg_index = mux->regs[i];

    if (reg_index) {
      uint32_t mask = (1 << mux->bits[i]);
      uint32_t regval = Read32GpioReg(gpio_block->mmio_index, reg_index);

      if (i == function - 1) {
        regval |= mask;
      } else {
        regval &= ~mask;
      }
      Write32GpioReg(gpio_block->mmio_index, reg_index, regval);
    }
  }

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplRead(uint32_t pin, uint8_t* out_value) {
  zx_status_t status;

  const AmlGpioBlock* block;
  uint32_t pin_index;
  fbl::Mutex* block_lock;
  if ((status = AmlPinToBlock(pin, &block, &pin_index, &block_lock)) != ZX_OK) {
    zxlogf(ERROR, "AmGxlGpio::GpioImplRead: pin not found %u", pin);
    return status;
  }

  const uint32_t readmask = 1 << pin_index;
  block_lock->Acquire();

  const uint32_t regval = Read32GpioReg(block->mmio_index, block->input_offset);

  block_lock->Release();

  if (regval & readmask) {
    *out_value = 1;
  } else {
    *out_value = 0;
  }

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplWrite(uint32_t pin, uint8_t value) {
  zx_status_t status;

  const AmlGpioBlock* block;
  uint32_t pin_index;
  fbl::Mutex* block_lock;
  if ((status = AmlPinToBlock(pin, &block, &pin_index, &block_lock)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::GpioImplWrite: pin not found %u", pin);
    return status;
  }

  if (block->output_write_shift) {
    // Handling special case where output_offset is
    // different for OEN/OUT for GPIOA0 block
    pin_index += block->output_write_shift;
  }

  fbl::AutoLock al(block_lock);

  uint32_t regval = Read32GpioReg(block->mmio_index, block->output_offset);
  if (value) {
    regval |= (1 << pin_index);
  } else {
    regval &= ~(1 << pin_index);
  }
  Write32GpioReg(block->mmio_index, block->output_offset, regval);

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplGetInterrupt(uint32_t pin, uint32_t flags, zx::interrupt* out_irq) {
  if (pin > kMaxGpioIndex) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock al(&interrupt_lock_);

  uint32_t index = GetUnusedIrqIndex(irq_status_);
  if (index > irq_info_.size()) {
    return ZX_ERR_NO_RESOURCES;
  }

  for (uint32_t i = 0; i < irq_info_.size(); i++) {
    if (irq_info_[i] == pin) {
      zxlogf(ERROR, "GPIO Interrupt already configured for this pin %u", (int)index);
      return ZX_ERR_ALREADY_EXISTS;
    }
  }

  zxlogf(TRACE, "GPIO Interrupt index %d allocated", (int)index);

  zx_status_t status;
  const AmlGpioBlock* block;
  uint32_t pin_index;
  fbl::Mutex* block_lock;
  if ((status = AmlPinToBlock(pin, &block, &pin_index, &block_lock)) != ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::GpioImplGetInterrupt: pin not found %u", pin);
    return status;
  }

  uint32_t flags_ = flags;
  if (flags == ZX_INTERRUPT_MODE_EDGE_LOW) {
    // GPIO controller sets the polarity
    flags_ = ZX_INTERRUPT_MODE_EDGE_HIGH;
  } else if (flags == ZX_INTERRUPT_MODE_LEVEL_LOW) {
    flags_ = ZX_INTERRUPT_MODE_LEVEL_HIGH;
  }

  // Create Interrupt Object
  if ((status = pdev_get_interrupt(&pdev_, index, flags_, out_irq->reset_and_get_address())) !=
      ZX_OK) {
    zxlogf(ERROR, "AmlGxlGpio::GpioImplGetInterrupt: pdev_get_interrupt failed %d", status);
    return status;
  }

  // Configure GPIO interrupt
  uint32_t pin_select_offset =
      (index > 3) ? gpio_interrupt_->pin_4_7_select_offset : gpio_interrupt_->pin_0_3_select_offset;

  // Select GPIO IRQ(index) and program it to
  // the requested GPIO PIN
  mmio_interrupt_.ModifyBits((pin % kPinsPerBlock) + block->pin_start,
                             index * kBitsPerGpioInterrupt, kBitsPerGpioInterrupt,
                             pin_select_offset << 2);
  // Configure GPIO Interrupt EDGE and Polarity
  uint32_t mode_reg_val = Read32GpioInterruptReg(gpio_interrupt_->edge_polarity_offset);

  switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      mode_reg_val = mode_reg_val | (1 << index);
      mode_reg_val = mode_reg_val | ((1 << index) << kGpioInterruptPolarityShift);
      break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      mode_reg_val = mode_reg_val | (1 << index);
      mode_reg_val = mode_reg_val & ~((1 << index) << kGpioInterruptPolarityShift);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      mode_reg_val = mode_reg_val & ~(1 << index);
      mode_reg_val = mode_reg_val | ((1 << index) << kGpioInterruptPolarityShift);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      mode_reg_val = mode_reg_val & ~(1 << index);
      mode_reg_val = mode_reg_val & ~((1 << index) << kGpioInterruptPolarityShift);
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  Write32GpioInterruptReg(gpio_interrupt_->edge_polarity_offset, mode_reg_val);

  // Configure Interrupt Select Filter
  uint32_t regval = Read32GpioInterruptReg(gpio_interrupt_->filter_select_offset);
  Write32GpioInterruptReg(gpio_interrupt_->filter_select_offset,
                          regval | (0x7 << (index * kBitsPerFilterSelect)));
  irq_status_ |= static_cast<uint8_t>(1 << index);
  irq_info_[index] = static_cast<uint16_t>(pin);

  return ZX_OK;
}

zx_status_t AmlGxlGpio::GpioImplReleaseInterrupt(uint32_t pin) {
  fbl::AutoLock al(&interrupt_lock_);

  for (uint32_t i = 0; i < irq_info_.size(); i++) {
    if (irq_info_[i] == pin) {
      irq_status_ &= static_cast<uint8_t>(~(1 << i));
      irq_info_[i] = kMaxGpioIndex + 1;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

zx_status_t AmlGxlGpio::GpioImplSetPolarity(uint32_t pin, uint32_t polarity) {
  int irq_index = -1;
  if (pin > kMaxGpioIndex) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (uint32_t i = 0; i < irq_info_.size(); i++) {
    if (irq_info_[i] == pin) {
      irq_index = i;
      break;
    }
  }

  if (irq_index == -1) {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::AutoLock al(&interrupt_lock_);

  // Configure GPIO Interrupt EDGE and Polarity
  uint32_t mode_reg_val = Read32GpioInterruptReg(gpio_interrupt_->edge_polarity_offset);
  if (polarity == GPIO_POLARITY_HIGH) {
    mode_reg_val &= ~((1 << irq_index) << kGpioInterruptPolarityShift);
  } else {
    mode_reg_val |= ((1 << irq_index) << kGpioInterruptPolarityShift);
  }

  Write32GpioInterruptReg(gpio_interrupt_->edge_polarity_offset, mode_reg_val);

  return ZX_OK;
}

zx_status_t aml_gpio_bind(void* ctx, zx_device_t* parent) {
  return gpio::AmlGxlGpio::Create(parent);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = aml_gpio_bind;
  return ops;
}();

}  // namespace gpio

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_gpio, gpio::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_GPIO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S912),
ZIRCON_DRIVER_END(aml_gpio)

