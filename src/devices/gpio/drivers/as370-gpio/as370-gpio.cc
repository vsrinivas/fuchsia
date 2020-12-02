// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-gpio.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/platform/bus.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "as370-gpio-reg.h"
#include "src/devices/gpio/drivers/as370-gpio/as370-gpio-bind.h"

namespace {

constexpr zx_off_t kGpioSwPortADr = 0x00;
constexpr zx_off_t kGpioSwPortADdr = 0x04;

constexpr zx_off_t kGpioPortAIntrEn = 0x30;
constexpr zx_off_t kGpioPortAIntrLevel = 0x38;     // 0 - level(default) , 1 - edge
constexpr zx_off_t kGpioPortAIntrPolarity = 0x3c;  // 0 - active low(default), 1 - active high
constexpr zx_off_t kGpioPortAIntrStatus = 0x40;
constexpr zx_off_t kGpioPortAIntrClear = 0x4c;

constexpr zx_off_t kGpioExtPortA = 0x50;

constexpr uint32_t kInterruptsPerPort = 16;

constexpr uint32_t kPinmuxFunctionWidth = 3;
constexpr uint32_t kPinmuxPinsPerReg = 10;

// The GPIO port index is used as the key for the interrupt port, from 0 to kMaxGpioPorts - 1. Key
// kMaxGpioPorts is used to tell the interrupt thread to exit when the driver is shutting down.
constexpr uint32_t kPortKeyTerminate = synaptics::kMaxGpioPorts;

}  // namespace

namespace gpio {

zx_status_t As370Gpio::Create(void* ctx, zx_device_t* parent) {
  synaptics::PinmuxMetadata pinmux_metadata = {};
  size_t actual;
  zx_status_t status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &pinmux_metadata,
                                           sizeof(pinmux_metadata), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get metadata: %d", __FILE__, status);
    return status;
  }
  if (actual != sizeof(pinmux_metadata)) {
    zxlogf(ERROR, "%s: Unexpected metadata size", __FILE__);
    return ZX_ERR_INTERNAL;
  }

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_DEVICE", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  pdev_device_info_t device_info = {};
  if ((status = pdev.GetDeviceInfo(&device_info)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to get device info: %d", __FILE__, status);
    return status;
  }

  const uint32_t pinmux_mmio_count = pinmux_metadata.muxes;
  const uint32_t gpio_mmio_count = device_info.mmio_count - pinmux_mmio_count;

  if (gpio_mmio_count > synaptics::kMaxGpioPorts) {
    zxlogf(ERROR, "%s: Too many GPIO MMIOs specified", __FILE__);
    return ZX_ERR_INTERNAL;
  }
  if (gpio_mmio_count < device_info.irq_count) {
    zxlogf(ERROR, "%s: Too many interrupts specified", __FILE__);
    return ZX_ERR_INTERNAL;
  }

  fbl::AllocChecker ac;

  fbl::Vector<ddk::MmioBuffer> pinmux_mmios;
  pinmux_mmios.reserve(pinmux_mmio_count, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Allocation failed", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  for (uint32_t i = 0; i < pinmux_mmio_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to map pinmux MMIO: %d", __FILE__, status);
      return status;
    }
    pinmux_mmios.push_back(*std::move(mmio));
  }

  fbl::Vector<ddk::MmioBuffer> gpio_mmios;
  gpio_mmios.reserve(gpio_mmio_count, &ac);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Allocation failed", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  for (uint32_t i = pinmux_mmio_count; i < pinmux_mmio_count + gpio_mmio_count; i++) {
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = pdev.MapMmio(i, &mmio)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to map GPIO MMIO: %d", __FILE__, status);
      return status;
    }
    gpio_mmios.push_back(*std::move(mmio));
  }

  fbl::Array<zx::interrupt> port_interrupts(new (&ac) zx::interrupt[device_info.irq_count],
                                            device_info.irq_count);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Allocation failed", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  for (uint32_t i = 0; i < port_interrupts.size(); i++) {
    zx::interrupt interrupt;
    if ((status = pdev.GetInterrupt(i, &interrupt)) != ZX_OK) {
      zxlogf(ERROR, "%s: Failed to get interrupt: %d", __FILE__, status);
      return status;
    }
    port_interrupts[i] = std::move(interrupt);
  }

  auto device = fbl::make_unique_checked<As370Gpio>(&ac, parent, std::move(pinmux_mmios),
                                                    std::move(gpio_mmios),
                                                    std::move(port_interrupts), pinmux_metadata);
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Failed to allocate device memory", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "%s: Init failed: %d", __FILE__, status);
    return status;
  }

  if ((status = device->Bind()) != ZX_OK) {
    zxlogf(ERROR, "%s: Bind failed: %d", __FILE__, status);
    device->Shutdown();
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

zx_status_t As370Gpio::Init() {
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_port_create failed %d", __FUNCTION__, status);
    return status;
  }

  for (const ddk::MmioBuffer& gpio_mmio : gpio_mmios_) {
    // Reset interrupt enable register
    gpio_mmio.Write32(0x0, kGpioPortAIntrEn);
  }

  uint32_t port_key = 0;
  for (const zx::interrupt& port_interrupt : port_interrupts_) {
    status = port_interrupt.bind(port_, port_key++, 0 /*options*/);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s zx_interrupt_bind failed %d", __FUNCTION__, status);
      return status;
    }
  }

  const size_t interrupt_count = kInterruptsPerPort * port_interrupts_.size();

  fbl::AllocChecker ac;
  gpio_interrupts_ = fbl::Array(new (&ac) zx::interrupt[interrupt_count], interrupt_count);
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
    zxlogf(ERROR, "%s: Failed to get ZX_PROTOCOL_PLATFORM_BUS", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  zx_status_t status;
  if ((status = DdkAdd("as370-gpio")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __FILE__, status);
    return status;
  }

  gpio_impl_protocol_t gpio_proto = {.ops = &gpio_impl_protocol_ops_, .ctx = this};
  status = pbus.RegisterProtocol(ddk_proto_id_, &gpio_proto, sizeof(gpio_proto));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to register ZX_PROTOCOL_GPIO_IMPL: %d", __FILE__, __LINE__);
    return status;
  }

  return ZX_OK;
}

int As370Gpio::Thread() {
  while (1) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed: %d", __FUNCTION__, status);
      return thrd_error;
    }

    if (packet.key == kPortKeyTerminate) {
      zxlogf(INFO, "As370Gpio thread terminating");
      return thrd_success;
    }
    if (packet.key >= gpio_mmios_.size()) {
      zxlogf(WARNING, "%s received interrupt from invalid port", __FUNCTION__);
      continue;
    }

    const uint32_t irq = gpio_mmios_[packet.key].Read32(kGpioPortAIntrStatus);
    const uint64_t interrupt_offset = packet.key * kInterruptsPerPort;
    const uint64_t gpio_offset = packet.key * synaptics::kGpiosPerPort;

    for (uint32_t index = 0; index < kInterruptsPerPort; index++) {
      if (irq & (1 << index)) {
        // Notify if interrupt is enabled for the GPIO pin.
        if (IsInterruptEnabled(gpio_offset + index)) {
          status = gpio_interrupts_[interrupt_offset + index].trigger(
              0, zx::time(packet.interrupt.timestamp));
          if (status != ZX_OK) {
            zxlogf(ERROR, "%s zx_interrupt_trigger failed %d", __func__, status);
          }
        }
        // Clear the interrupt.
        gpio_mmios_[packet.key].ModifyBit<uint32_t>(true, index, kGpioPortAIntrClear);
      }
    }
    port_interrupts_[packet.key].ack();
  }
  return thrd_success;
}

void As370Gpio::SetInterruptPolarity(uint32_t index, bool is_high) {
  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[index / synaptics::kGpiosPerPort];
  const uint32_t bit = index % synaptics::kGpiosPerPort;
  gpio_mmio.ModifyBit<uint32_t>(is_high, bit, kGpioPortAIntrPolarity);
}

void As370Gpio::SetInterruptEdge(uint32_t index, bool is_edge) {
  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[index / synaptics::kGpiosPerPort];
  const uint32_t bit = index % synaptics::kGpiosPerPort;
  gpio_mmio.ModifyBit<uint32_t>(is_edge, bit, kGpioPortAIntrLevel);
}

bool As370Gpio::IsInterruptEnabled(uint64_t index) {
  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[index / synaptics::kGpiosPerPort];
  const uint32_t bit = index % synaptics::kGpiosPerPort;
  return gpio_mmio.Read32(kGpioPortAIntrEn) & (1 << bit);
}

zx_status_t As370Gpio::GpioImplConfigIn(uint32_t index, uint32_t flags) {
  if ((flags & GPIO_PULL_MASK) != GPIO_NO_PULL) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[port];
  const uint32_t bit = index % synaptics::kGpiosPerPort;
  gpio_mmio.ClearBit<uint32_t>(bit, kGpioSwPortADdr);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  GpioImplWrite(index, initial_value);

  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[port];
  const uint32_t bit = index - (port * synaptics::kGpiosPerPort);
  gpio_mmio.SetBit<uint32_t>(bit, kGpioSwPortADdr);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetAltFunction(uint32_t index, uint64_t function) {
  if (index >= std::size(pinmux_metadata_.pinmux_map) || function >= (1 << kPinmuxFunctionWidth)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const synaptics::PinmuxEntry& entry = pinmux_metadata_.pinmux_map[index];
  if (entry.type == synaptics::PinmuxEntry::kInvalid) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (entry.pinmux_mmio >= pinmux_mmios_.size()) {
    return ZX_ERR_INTERNAL;
  }

  const uint32_t pinmux_reg = sizeof(uint32_t) * (entry.pinmux_index / kPinmuxPinsPerReg);
  const uint32_t pinmux_bit = (entry.pinmux_index % kPinmuxPinsPerReg) * kPinmuxFunctionWidth;

  const ddk::MmioBuffer& pinmux_mmio = pinmux_mmios_[entry.pinmux_mmio];
  pinmux_mmio.ModifyBits<uint32_t>(static_cast<uint32_t>(function), pinmux_bit,
                                   kPinmuxFunctionWidth, pinmux_reg);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetDriveStrength(uint32_t index, uint64_t ua,
                                                uint64_t* out_actual_ua) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t As370Gpio::GpioImplRead(uint32_t index, uint8_t* out_value) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[port];
  const uint32_t mask = 1 << (index % synaptics::kGpiosPerPort);
  *out_value = ((gpio_mmio.Read32(kGpioExtPortA) & mask) == 0) ? 0 : 1;

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplWrite(uint32_t index, uint8_t value) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const ddk::MmioBuffer& gpio_mmio = gpio_mmios_[port];
  const uint32_t bit = index % synaptics::kGpiosPerPort;
  gpio_mmio.ModifyBit<uint32_t>(value, bit, kGpioSwPortADr);

  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplGetInterrupt(uint32_t index, uint32_t flags,
                                            zx::interrupt* out_irq) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint32_t bit = index % synaptics::kGpiosPerPort;
  const uint32_t interrupt_index = bit + (port * kInterruptsPerPort);
  if (bit >= kInterruptsPerPort || interrupt_index >= gpio_interrupts_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (IsInterruptEnabled(index)) {
    zxlogf(ERROR, "%s interrupt %u already exists", __FUNCTION__, index);
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx::interrupt irq;
  zx_status_t status = zx::interrupt::create(zx::resource(), index, ZX_INTERRUPT_VIRTUAL, &irq);
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
  gpio_interrupts_[interrupt_index] = std::move(irq);
  gpio_mmios_[port].ModifyBit<uint32_t>(true, bit, kGpioPortAIntrEn);
  zxlogf(DEBUG, "%s INT %u enabled", __FUNCTION__, index);
  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplReleaseInterrupt(uint32_t index) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint32_t bit = index % synaptics::kGpiosPerPort;
  const uint32_t interrupt_index = bit + (port * kInterruptsPerPort);
  if (bit >= kInterruptsPerPort || interrupt_index >= gpio_interrupts_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (!IsInterruptEnabled(index)) {
    return ZX_ERR_BAD_STATE;
  }

  gpio_mmios_[port].ModifyBit<uint32_t>(false, bit, kGpioPortAIntrEn);
  gpio_interrupts_[interrupt_index].destroy();
  gpio_interrupts_[interrupt_index].reset();
  return ZX_OK;
}

zx_status_t As370Gpio::GpioImplSetPolarity(uint32_t index, gpio_polarity_t polarity) {
  if (index >= std::size(pinmux_metadata_.pinmux_map)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (pinmux_metadata_.pinmux_map[index].type != synaptics::PinmuxEntry::kGpio) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  const uint32_t port = index / synaptics::kGpiosPerPort;
  if (port >= gpio_mmios_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  const uint32_t bit = index % synaptics::kGpiosPerPort;
  const uint32_t interrupt_index = bit + (port * kInterruptsPerPort);
  if (bit >= kInterruptsPerPort || interrupt_index >= gpio_interrupts_.size()) {
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
  thrd_join(thread_, nullptr);
}

void As370Gpio::DdkUnbind(ddk::UnbindTxn txn) {
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
ZIRCON_DRIVER(as370_gpio, as370_gpio_driver_ops, "zircon", "0.1");
