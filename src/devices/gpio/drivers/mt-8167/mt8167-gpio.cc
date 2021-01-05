// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167-gpio.h"

#include <stdint.h>
#include <threads.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <hw/reg.h>
#include <soc/mt8167/mt8167-hw.h>

namespace gpio {

int Mt8167GpioDevice::Thread() {
  while (1) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d", __FUNCTION__, status);
      return thrd_error;
    }
    uint32_t index = eint_.GetNextInterrupt(0);
    while (index != ExtendedInterruptReg::kInvalidInterruptIdx && index < interrupts_.size() &&
           interrupts_[index].is_valid()) {
      zxlogf(DEBUG, "%s msg on port key %lu  EINT %u", __FUNCTION__, packet.key, index);
      if (eint_.IsEnabled(index)) {
        zxlogf(DEBUG, "%s zx_interrupt_trigger for %u", __FUNCTION__, index);
        status = interrupts_[index].trigger(0, zx::time(packet.interrupt.timestamp));
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s zx_interrupt_trigger failed %d ", __FUNCTION__, status);
        }
      }
      eint_.AckInterrupt(index);
      index = eint_.GetNextInterrupt(index + 1);
    }
    int_.ack();
  }
}

zx_status_t Mt8167GpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  GpioModeReg::SetMode(&gpio_mmio_, index, GpioModeReg::kModeGpio);
  dir_.SetDir(index, false);
  const uint32_t pull_mode = flags & GPIO_PULL_MASK;

  switch (pull_mode) {
    case GPIO_NO_PULL:
      if (pull_en_.PullDisable(index)) {
        return ZX_OK;
      }
      break;
    case GPIO_PULL_UP:
      if (pull_en_.PullEnable(index) && pull_sel_.SetPullUp(index)) {
        return ZX_OK;
      }
      break;
    case GPIO_PULL_DOWN:
      if (pull_en_.PullEnable(index) && pull_sel_.SetPullDown(index)) {
        return ZX_OK;
      }
      break;
  }

  if (iocfg_) {
    // If not supported above, try IO Config.
    // TODO(andresoportus): We only support enable/disable pull through the GPIO protocol, so
    // until we allow passing particular pull amounts we can specify here different pull amounts
    // for particular GPIOs.
    PullAmount pull_amount = kPull10K;
    if (index >= 40 && index <= 43) {
      pull_amount = kPull75K;
    }
    switch (pull_mode) {
      case GPIO_NO_PULL:
        if (iocfg_->PullDisable(index)) {
          return ZX_OK;
        }
        break;
      case GPIO_PULL_UP:
        if (iocfg_->PullEnable(index, pull_amount) && iocfg_->SetPullUp(index)) {
          return ZX_OK;
        }
        break;
      case GPIO_PULL_DOWN:
        if (iocfg_->PullEnable(index, pull_amount) && iocfg_->SetPullDown(index)) {
          return ZX_OK;
        }
        break;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167GpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  GpioModeReg::SetMode(&gpio_mmio_, index, GpioModeReg::kModeGpio);
  dir_.SetDir(index, true);
  return GpioImplWrite(index, initial_value);
}

zx_status_t Mt8167GpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (function >= GpioModeReg::kModeMax) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  GpioModeReg::SetMode(&gpio_mmio_, index, static_cast<uint16_t>(function));
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_value = static_cast<uint8_t>(in_.GetVal(index));
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  out_.SetVal(index, value);
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                   zx::interrupt* out_irq) {
  zx_status_t status;
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (eint_.IsEnabled(index)) {
    zxlogf(ERROR, "%s interrupt %u already exists", __FUNCTION__, index);
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx::interrupt irq;
  status = zx::interrupt::create(zx::resource(), index, ZX_INTERRUPT_VIRTUAL, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx::interrupt::create failed %d ", __FUNCTION__, status);
    return status;
  }
  status = irq.duplicate(ZX_RIGHT_SAME_RIGHTS, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt.duplicate failed %d ", __FUNCTION__, status);
    return status;
  }

  switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      eint_.SetPolarity(index, false);
      eint_.SetEdge(index, true);
      break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      eint_.SetPolarity(index, true);
      eint_.SetEdge(index, true);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      eint_.SetPolarity(index, false);
      eint_.SetEdge(index, false);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      eint_.SetPolarity(index, true);
      eint_.SetEdge(index, false);
      break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  interrupts_[index] = std::move(irq);
  eint_.Enable(index);
  zxlogf(DEBUG, "%s EINT %u enabled", __FUNCTION__, index);
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
  if (index >= interrupts_.size() || !eint_.IsEnabled(index)) {
    return ZX_ERR_INVALID_ARGS;
  }
  eint_.Disable(index);
  interrupts_[index].destroy();
  interrupts_[index].reset();
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
  if (index >= interrupts_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (polarity == GPIO_POLARITY_LOW) {
    eint_.SetPolarity(index, false);
    return ZX_OK;
  } else if (polarity == GPIO_POLARITY_HIGH) {
    eint_.SetPolarity(index, true);
    return ZX_OK;
  }
  return ZX_ERR_INVALID_ARGS;
}

void Mt8167GpioDevice::ShutDown() {
  int_.destroy();
  thrd_join(thread_, NULL);
}

void Mt8167GpioDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void Mt8167GpioDevice::DdkRelease() { delete this; }

zx_status_t Mt8167GpioDevice::Bind() {
  pdev_protocol_t pdev;
  zx_status_t status = device_get_protocol(parent(), ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d ", __FUNCTION__, status);
    return status;
  }

  status = pdev_get_interrupt(&pdev, 0, 0, int_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pdev_get_interrupt failed %d", __FUNCTION__, status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_port_create failed %d", __FUNCTION__, status);
    return status;
  }

  status = int_.bind(port_, 0, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_interrupt_bind failed %d", __FUNCTION__, status);
    return status;
  }
  fbl::AllocChecker ac;
  interrupts_ = fbl::Array(new (&ac) zx::interrupt[MT8167_GPIO_EINT_MAX], MT8167_GPIO_EINT_MAX);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  for (uint32_t i = 0; i < interrupts_.size(); ++i) {
    eint_.SetDomain0(i);
    eint_.Disable(i);
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<Mt8167GpioDevice*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "mt8167-gpio-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  status = DdkAdd("mt8167-gpio");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed %d", __FUNCTION__, status);
    ShutDown();
    return status;
  }
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::Init() {
  zx_status_t status;
  pbus_protocol_t pbus;
  status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d", __FUNCTION__, status);
    return status;
  }
  gpio_impl_protocol_t gpio_proto = {
      .ops = &gpio_impl_protocol_ops_,
      .ctx = this,
  };
  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_register_protocol failed %d", __FUNCTION__, status);
    ShutDown();
    return status;
  }
  return ZX_OK;
}

zx_status_t Mt8167GpioDevice::Create(zx_device_t* parent) {
  pdev_protocol_t pdev;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d ", __FUNCTION__, status);
    return status;
  }

  pdev_device_info_t info;
  status = pdev_get_device_info(&pdev, &info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pdev_get_device_info failed %d ", __FUNCTION__, status);
    return status;
  }

  int mmioindex = 0;

  mmio_buffer_t gpio_mmio;
  mmio_buffer_t iocfg_mmio;
  mmio_buffer_t eint_mmio;

  status = pdev_map_mmio_buffer(&pdev, mmioindex++, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s gpio pdev_map_mmio_buffer failed %d", __FUNCTION__, status);
    return status;
  }

  if (info.mmio_count == 3) {
    status = pdev_map_mmio_buffer(&pdev, mmioindex++, ZX_CACHE_POLICY_UNCACHED_DEVICE, &iocfg_mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s iocfg pdev_map_mmio_buffer failed %d", __FUNCTION__, status);
      return status;
    }
  }

  status = pdev_map_mmio_buffer(&pdev, mmioindex++, ZX_CACHE_POLICY_UNCACHED_DEVICE, &eint_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_map_mmio_buffer gpio failed %d", __FUNCTION__, status);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<gpio::Mt8167GpioDevice> dev;

  if (info.mmio_count == 3) {
    dev = fbl::make_unique_checked<gpio::Mt8167GpioDevice>(&ac, parent, ddk::MmioBuffer(gpio_mmio),
                                                           ddk::MmioBuffer(iocfg_mmio),
                                                           ddk::MmioBuffer(eint_mmio));
  } else {
    dev = fbl::make_unique_checked<gpio::Mt8167GpioDevice>(&ac, parent, ddk::MmioBuffer(gpio_mmio),
                                                           ddk::MmioBuffer(eint_mmio));
  }
  if (!ac.check()) {
    zxlogf(ERROR, "mt8167_gpio_bind: ZX_ERR_NO_MEMORY");
    return ZX_ERR_NO_MEMORY;
  }
  status = dev->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev
  auto ptr = dev.release();
  return ptr->Init();
}

zx_status_t mt8167_gpio_bind(void* ctx, zx_device_t* parent) {
  return gpio::Mt8167GpioDevice::Create(parent);
}

}  // namespace gpio
