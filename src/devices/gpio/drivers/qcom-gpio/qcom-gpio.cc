// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "qcom-gpio.h"

#include <algorithm>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>

namespace {

uint64_t kPortKeyIrqMsg = 0x00;
uint64_t kPortKeyTerminate = 0x01;

}  // namespace

namespace gpio {

int QcomGpioDevice::Thread() {
  while (1) {
    zx_port_packet_t packet;
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d", __func__, status);
      return thrd_error;
    }
    zxlogf(DEBUG, "%s msg on port key %lu", __func__, packet.key);
    if (packet.key == kPortKeyTerminate) {
      zxlogf(INFO, "QCOM GPIO thread terminating");
      return 0;
    }
    size_t index = 0;
    status = enabled_ints_cache_.Find(true, 0, kGpioMax, 1, &index);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s no interrupt found in cache %d", __func__, status);
    }
    while (status == ZX_OK) {
      zxlogf(DEBUG, "%s msg on port INT %lu", __func__, index);
      if (status_int_.Status(index)) {
        status = interrupts_[index].trigger(0, zx::time(packet.interrupt.timestamp));
        if (status != ZX_OK) {
          zxlogf(ERROR, "%s zx_interrupt_trigger failed %d", __func__, status);
        }
        status_int_.Clear(index);
      } else {
        zxlogf(ERROR, "%s interrupt %lu not enabled in reg", __func__, index);
      }
      status = enabled_ints_cache_.Find(true, index + 1, kGpioMax, 1, &index);
      if (status != ZX_ERR_NO_RESOURCES) {  // not just not-found in cache.
        zxlogf(ERROR, "%s error in reading from cache %d", __func__, status);
      }
    }
    combined_int_.ack();
  }
}

zx_status_t QcomGpioDevice::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  GpioCfgReg::SetMode(&gpio_mmio_, index, GpioCfgReg::kModeGpio);
  GpioCfgReg::SetOut(&gpio_mmio_, index, false);
  const uint32_t pull_mode = flags & GPIO_PULL_MASK;

  // clang-format off
    switch (pull_mode) {
    case GPIO_NO_PULL:   GpioCfgReg::SetPullNone(&gpio_mmio_, index); break;
    case GPIO_PULL_DOWN: GpioCfgReg::SetPullDown(&gpio_mmio_, index); break;
    case GPIO_PULL_UP:   GpioCfgReg::SetPullUp  (&gpio_mmio_, index); break;
    default: return ZX_ERR_NOT_SUPPORTED;
    }
  // clang-format on
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  GpioCfgReg::SetMode(&gpio_mmio_, index, GpioCfgReg::kModeGpio);
  GpioCfgReg::SetOut(&gpio_mmio_, index, true);
  return GpioImplWrite(index, initial_value);
}

zx_status_t QcomGpioDevice::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (function >= GpioCfgReg::kModeMax) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  GpioCfgReg::SetMode(&gpio_mmio_, index, static_cast<uint32_t>(function));
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplSetDriveStrength(uint32_t index, uint64_t ua,
                                                     uint64_t* out_actual_ua) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  uint64_t supported_uas[] = {2000, 4000, 6000, 8000, 10000, 12000, 14000, 16000};
  if (std::find(std::begin(supported_uas), std::end(supported_uas), ua) ==
      std::end(supported_uas)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  GpioCfgReg::SetStrength(&gpio_mmio_, index, static_cast<uint8_t>(ua / 1000));
  if (out_actual_ua) {
    *out_actual_ua = ua;
  }
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplRead(uint32_t index, uint8_t* out_value) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out_value = static_cast<uint8_t>(in_out_.GetVal(index));
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplWrite(uint32_t index, uint8_t value) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  in_out_.SetVal(index, value);
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                                 zx::interrupt* out_irq) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::interrupt irq;
  auto status = zx::interrupt::create(zx::resource(), index, ZX_INTERRUPT_VIRTUAL, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx::interrupt::create failed %d", __func__, status);
    return status;
  }
  status = irq.duplicate(ZX_RIGHT_SAME_RIGHTS, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt.duplicate failed %d", __func__, status);
    return status;
  }

  // clang-format off
    switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:   int_cfg_.SetMode(index, Mode::EdgeLow);   break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:  int_cfg_.SetMode(index, Mode::EdgeHigh);  break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:  int_cfg_.SetMode(index, Mode::LevelLow);  break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH: int_cfg_.SetMode(index, Mode::LevelHigh); break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:  int_cfg_.SetMode(index, Mode::EdgeDual);  break;
    default: return ZX_ERR_INVALID_ARGS;
    }
  // clang-format on

  interrupts_[index] = std::move(irq);
  // TODO(andresoportus): Once we define which cases would use them, enable direct interrupts
  // (via TlmmDirConnIntReg).
  status_int_.Clear(index);
  int_cfg_.EnableCombined(index, true);
  enabled_ints_cache_.SetOne(index);
  zxlogf(DEBUG, "%s INT %u enabled", __func__, index);
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplReleaseInterrupt(uint32_t index) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  interrupts_[index].destroy();
  interrupts_[index].reset();
  int_cfg_.EnableCombined(index, false);
  enabled_ints_cache_.ClearOne(index);
  zxlogf(DEBUG, "%s INT %u disabled", __func__, index);
  return ZX_OK;
}

zx_status_t QcomGpioDevice::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {
  if (index >= kGpioMax) {
    return ZX_ERR_INVALID_ARGS;
  }
  int_cfg_.SetPolarity(index, static_cast<bool>(polarity));
  return ZX_OK;
}

void QcomGpioDevice::ShutDown() {
  combined_int_.destroy();
  zx_port_packet packet = {kPortKeyTerminate, ZX_PKT_TYPE_USER, ZX_OK, {}};
  auto status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  thrd_join(thread_, NULL);
}

void QcomGpioDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void QcomGpioDevice::DdkRelease() { delete this; }

zx_status_t QcomGpioDevice::Bind() {
  auto status = pdev_.GetInterrupt(0, &combined_int_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s GetInterrupt failed %d", __func__, status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port create failed %d", __func__, status);
    return status;
  }

  status = combined_int_.bind(port_, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt bind failed %d", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  interrupts_ = fbl::Array(new (&ac) zx::interrupt[kGpioMax], kGpioMax);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<QcomGpioDevice*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "qcom-gpio-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  status = DdkAdd("qcom-gpio");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed %d", __func__, status);
    ShutDown();
    return status;
  }
  return ZX_OK;
}

zx_status_t QcomGpioDevice::Init() {
  pbus_protocol_t pbus;
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PBUS not available %d", __func__, status);
    return status;
  }
  gpio_impl_protocol_t gpio_proto = {
      .ops = &gpio_impl_protocol_ops_,
      .ctx = this,
  };
  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pbus_register_protocol failed %d", __func__, status);
    ShutDown();
    return status;
  }
  return enabled_ints_cache_.Reset(kGpioMax);  // Clear and resize.
}

zx_status_t QcomGpioDevice::Create(zx_device_t* parent) {
  pdev_protocol_t pdev;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s ZX_PROTOCOL_PDEV not available %d", __func__, status);
    return status;
  }

  mmio_buffer_t gpio_mmio;
  status = pdev_map_mmio_buffer(&pdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &gpio_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s gpio pdev_map_mmio_buffer failed %d", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto dev =
      fbl::make_unique_checked<gpio::QcomGpioDevice>(&ac, parent, ddk::MmioBuffer(gpio_mmio));
  if (!ac.check()) {
    zxlogf(ERROR, "qcom_gpio_bind: ZX_ERR_NO_MEMORY");
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

zx_status_t qcom_gpio_bind(void* ctx, zx_device_t* parent) {
  return gpio::QcomGpioDevice::Create(parent);
}

}  // namespace gpio
