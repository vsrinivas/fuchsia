// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/alloc_checker.h>

#include "as370-gpio-reg.h"

namespace {

constexpr zx_off_t kGpioSwPortADr = 0x00;
constexpr zx_off_t kGpioSwPortADdr = 0x04;

constexpr zx_off_t kGpioPortAIntrEn = 0x30;
constexpr zx_off_t kGpioPortAIntrLevel = 0x38;     // 0 - level(default) , 1 - edge
constexpr zx_off_t kGpioPortAIntrPolarity = 0x3c;  // 0 - active low(default), 1 - active high
constexpr zx_off_t kGpioPortAIntrStatus = 0x40;
constexpr zx_off_t kGpioPortAIntrClear = 0x4c;

constexpr zx_off_t kGpioExtPortA = 0x50;

constexpr zx_off_t kPinmuxCntlBusBase = 0x40;

constexpr uint32_t kPorts = 2;
constexpr uint32_t kGpiosPerPort = 32;
constexpr uint32_t kTotalPins = 72;
constexpr uint32_t kMaxInterruptPins = 16;

constexpr uint32_t kPinmuxFunctionWidth = 3;
constexpr uint32_t kPinmuxPinsPerReg = 10;

constexpr uint32_t kGpioPinmuxWindowOffset = 18;

// Maps possible drive strengths in milliamps to register values.
constexpr uint8_t kDriveStrengthMap[] = {2, 4, 8, 12};

uint32_t GetGpioBitOffset(uint32_t index) {
  return (index < kGpiosPerPort) ? index : (index - kGpiosPerPort);
}

uint32_t GpioToPinmuxIndex(uint32_t index) {
  // The pinmux registers have a gap with respect to the GPIOs, like this:
  // |----- GPIOs 0-17 -----|--- NAND pins ---|--------------- GPIOs 18-63 ---------------|
  // The NAND pins are mapped to GPIOs 64-71, so the index parameter must be adjusted accordingly.

  if (index >= kPorts * kGpiosPerPort) {
    return index - (kPorts * kGpiosPerPort) + kGpioPinmuxWindowOffset;
  }
  if (index >= kGpioPinmuxWindowOffset) {
    return index + kTotalPins - (kPorts * kGpiosPerPort);
  }
  return index;
}

constexpr uint32_t kPortKeyTerminate = 0x01;

}  // namespace

namespace gpio {

zx_status_t As370Gpio::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_DEVICE\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> pinctrl_mmio;
  zx_status_t status = pdev.MapMmio(0, &pinctrl_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map pinmux MMIO: %d\n", __FILE__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> gpio1_mmio;
  if ((status = pdev.MapMmio(1, &gpio1_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map GPIO 1 MMIO: %d\n", __FILE__, status);
    return status;
  }

  std::optional<ddk::MmioBuffer> gpio2_mmio;
  if ((status = pdev.MapMmio(2, &gpio2_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map GPIO 2 MMIO: %d\n", __FILE__, status);
    return status;
  }

  zx::interrupt gpio1_irq;
  if ((status = pdev.GetInterrupt(0, &gpio1_irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get interrupt: %d\n", __FILE__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<As370Gpio>(&ac, parent, *std::move(pinctrl_mmio),
                                                    *std::move(gpio1_mmio), *std::move(gpio2_mmio),
                                                    std::move(gpio1_irq));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate device memory\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed: %d\n", __FILE__, status);
    return status;
  }

  if ((status = device->Bind()) != ZX_OK) {
    zxlogf(ERROR, "%s: Bind failed: %d\n", __FILE__, status);
    device->Shutdown();
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

zx_status_t As370Gpio::Init() {
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_port_create failed %d\n", __FUNCTION__, status);
    return status;
  }

  // Reset interrupt enable register
  gpio1_mmio_.Write32(0x0, kGpioPortAIntrEn);

  status = gpio1_irq_.bind(port_, 0, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_interrupt_bind failed %d\n", __FUNCTION__, status);
    return status;
  }

  fbl::AllocChecker ac;
  interrupts_ = fbl::Array(new (&ac) zx::interrupt[kMaxInterruptPins], kMaxInterruptPins);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<As370Gpio*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "as370-gpio-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t As370Gpio::Bind() {
  ddk::PBusProtocolClient pbus(parent());
  if (!pbus.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_BUS\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status;
  if ((status = DdkAdd("as370-gpio")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d\n", __FILE__, status);
    return status;
  }

  gpio_impl_protocol_t gpio_proto = {.ops = &gpio_impl_protocol_ops_, .ctx = this};
  status = pbus.RegisterProtocol(ddk_proto_id_, &gpio_proto, sizeof(gpio_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to register ZX_PROTOCOL_GPIO_IMPL: %d\n", __FILE__, __LINE__);
    return status;
  }

  return ZX_OK;
}

int As370Gpio::Thread() {
  while (1) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d\n", __FUNCTION__, status);
      return thrd_error;
    }

    if (packet.key == kPortKeyTerminate) {
      zxlogf(INFO, "As370Gpio thread terminating\n");
      return thrd_success;
    }

    uint32_t irq = gpio1_mmio_.Read32(kGpioPortAIntrStatus);

    uint32_t index = 0;
    while (index < kMaxInterruptPins) {
      const uint32_t mask = 1 << GetGpioBitOffset(index);
      if (irq & mask) {
        // Notify if interrupt is enabled for the GPIO pin.
        if (IsInterruptEnabled(index)) {
          status = interrupts_[index].trigger(0, zx::time(packet.interrupt.timestamp));
          if (status != ZX_OK) {
            zxlogf(ERROR, "%s zx_interrupt_trigger failed %d\n", __func__, status);
          }
        }
        // Clear the interrupt.
        gpio1_mmio_.ModifyBit<uint32_t>(true, GetGpioBitOffset(index), kGpioPortAIntrClear);
      }
      index++;
    }
    gpio1_irq_.ack();
  }
  return thrd_success;
}

void As370Gpio::SetInterruptPolarity(uint32_t index, bool is_high) {
  gpio1_mmio_.ModifyBit<uint32_t>(is_high, GetGpioBitOffset(index), kGpioPortAIntrPolarity);
}

void As370Gpio::SetInterruptEdge(uint32_t index, bool is_edge) {
  gpio1_mmio_.ModifyBit<uint32_t>(is_edge, GetGpioBitOffset(index), kGpioPortAIntrLevel);
}

bool As370Gpio::IsInterruptEnabled(uint32_t index) {
  return gpio1_mmio_.Read32(kGpioPortAIntrEn) & (1 << GetGpioBitOffset(index));
}

zx_status_t As370Gpio::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  if (index >= kTotalPins) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  IoCntl::Get(GpioToPinmuxIndex(index))
      .ReadFrom(&pinmux_mmio_)
      .set_pden((flags & GPIO_PULL_MASK) == GPIO_PULL_DOWN ? 1 : 0)
      .set_puen((flags & GPIO_PULL_MASK) == GPIO_PULL_UP ? 1 : 0)
      .WriteTo(&pinmux_mmio_);

  // The eight NAND data pins aren't GPIOs and can't be set to input, however they still have
  // pull-up/down resistors. Just skip them and report ZX_OK.
  if (index < kPorts * kGpiosPerPort) {
    const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
    gpio_mmio.ClearBit<uint32_t>(GetGpioBitOffset(index), kGpioSwPortADdr);
  }

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  if (index >= kPorts * kGpiosPerPort) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  GpioImplWrite(index, initial_value);

  const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
  gpio_mmio.SetBit<uint32_t>(GetGpioBitOffset(index), kGpioSwPortADdr);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
  if (index >= kTotalPins || function >= (1 << kPinmuxFunctionWidth)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  index = GpioToPinmuxIndex(index);

  zx_off_t reg_offset = (sizeof(uint32_t) * (index / kPinmuxPinsPerReg)) + kPinmuxCntlBusBase;
  uint32_t bit_offset = (index % kPinmuxPinsPerReg) * kPinmuxFunctionWidth;
  pinmux_mmio_.ModifyBits<uint32_t>(static_cast<uint32_t>(function), bit_offset,
                                    kPinmuxFunctionWidth, reg_offset);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetDriveStrength(uint32_t index, uint8_t m_a) {
  if (index >= kTotalPins) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  for (uint32_t i = 0; i < fbl::count_of(kDriveStrengthMap); i++) {
    if (kDriveStrengthMap[i] == m_a) {
      IoCntl::Get(GpioToPinmuxIndex(index))
          .ReadFrom(&pinmux_mmio_)
          .set_drv(i)
          .WriteTo(&pinmux_mmio_);

      return ZX_OK;
    }
  }

  return ZX_ERR_INVALID_ARGS;
}

zx_status_t As370Gpio::GpioImplRead(uint32_t index, uint8_t* out_value) {
  if (index >= kPorts * kGpiosPerPort) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
  const uint32_t mask = 1 << GetGpioBitOffset(index);
  *out_value = ((gpio_mmio.Read32(kGpioExtPortA) & mask) == 0) ? 0 : 1;

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplWrite(uint32_t index, uint8_t value) {
  if (index >= kPorts * kGpiosPerPort) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const ddk::MmioBuffer& gpio_mmio = (index < kGpiosPerPort) ? gpio1_mmio_ : gpio2_mmio_;
  gpio_mmio.ModifyBit<uint32_t>(value, GetGpioBitOffset(index), kGpioSwPortADr);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                            zx::interrupt* out_irq) {
  // Only 0-15 pins of Port A supports interrupts
  if (index >= kMaxInterruptPins) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (IsInterruptEnabled(index)) {
    zxlogf(ERROR, "%s interrupt %u already exists\n", __FUNCTION__, index);
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx::interrupt irq;
  zx_status_t status = zx::interrupt::create(zx::resource(), index, ZX_INTERRUPT_VIRTUAL, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx::interrupt::create failed %d \n", __FUNCTION__, status);
    return status;
  }
  status = irq.duplicate(ZX_RIGHT_SAME_RIGHTS, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s interrupt.duplicate failed %d \n", __FUNCTION__, status);
    return status;
  }

  switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      SetInterruptPolarity(index, false);
      SetInterruptEdge(index, true);
      break;
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      SetInterruptPolarity(index, true);
      SetInterruptEdge(index, true);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      SetInterruptPolarity(index, false);
      SetInterruptEdge(index, false);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      SetInterruptPolarity(index, true);
      SetInterruptEdge(index, false);
      break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  interrupts_[index] = std::move(irq);
  gpio1_mmio_.ModifyBit<uint32_t>(true, GetGpioBitOffset(index), kGpioPortAIntrEn);
  zxlogf(TRACE, "%s INT %u enabled\n", __FUNCTION__, index);
  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplReleaseInterrupt(uint32_t index) {
  if (index >= kMaxInterruptPins) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (!IsInterruptEnabled(index)) {
    return ZX_ERR_BAD_STATE;
  }
  gpio1_mmio_.ModifyBit<uint32_t>(false, GetGpioBitOffset(index), kGpioPortAIntrEn);
  interrupts_[index].destroy();
  interrupts_[index].reset();
  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity) {
  if (index >= kMaxInterruptPins) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (polarity == GPIO_POLARITY_LOW) {
    SetInterruptPolarity(index, false);
    return ZX_OK;
  } else if (polarity == GPIO_POLARITY_HIGH) {
    SetInterruptPolarity(index, true);
    return ZX_OK;
  }
  return ZX_ERR_INVALID_ARGS;
}

void As370Gpio::Shutdown() {
  zx_port_packet packet = {kPortKeyTerminate, ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
  thrd_join(thread_, NULL);
}

void As370Gpio::DdkUnbindNew(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

void As370Gpio::DdkRelease() { delete this; }

}  // namespace gpio

static constexpr zx_driver_ops_t as370_gpio_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = gpio::As370Gpio::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(as370_gpio, as370_gpio_driver_ops, "zircon", "0.1", 2)
  BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
  BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_SYNAPTICS_GPIO),
ZIRCON_DRIVER_END(as370_gpio)
//clang-format on
