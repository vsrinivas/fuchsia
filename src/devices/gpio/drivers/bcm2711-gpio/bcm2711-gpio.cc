// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bcm2711-gpio.h"

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include <soc/bcm2711/bcm2711-gpio.h>
#include <soc/bcm2711/bcm2711-hw.h>

namespace gpio {

// MMIO as defined in rpi4 board driver
// defined only one contiguous space
enum {
  MMIO_GPIO = 0,                // started from BCM2711_GPIO_BASE
};

zx_status_t Bcm2711Gpio::Create(void* ctx, zx_device_t* parent) {

  zx_status_t status;
  pbus_protocol_t pbus;
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus)) != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: ZX_PROTOCOL_PBUS not available");
    return status;
  }

  ddk::PDev pdev(parent);
  std::optional<ddk::MmioBuffer> mmio_gpio;
  if ((status = pdev.MapMmio(MMIO_GPIO, &mmio_gpio)) != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: MapMmio failed");
    return status;
  }

  pdev_device_info_t info;
  if ((status = pdev.GetDeviceInfo(&info)) != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: GetDeviceInfo failed");
    return status;
  }

  fbl::AllocChecker ac;
  fbl::Array<zx::interrupt>
      port_interrupts(new (&ac) zx::interrupt[info.irq_count], info.irq_count);
  if (!ac.check()) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: port interrupts alloc failed");
    return ZX_ERR_NO_RESOURCES;
  }

  for (uint32_t i = 0; i < port_interrupts.size(); i++) {
    zx::interrupt interrupt;
    if ((status = pdev.GetInterrupt(i, &interrupt)) != ZX_OK) {
      zxlogf(ERROR, "Bcm2711Gpio::Create: GetInterrupt failed %d", status);
      return status;
    }
    port_interrupts[i] = std::move(interrupt);
  }

  std::unique_ptr<Bcm2711Gpio> device(new (&ac) Bcm2711Gpio(parent, 
          *std::move(mmio_gpio), std::move(port_interrupts)));
  if (!ac.check()) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: device object alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: Init failed");
    return status;
  }

  device->Bind(pbus);

  if ((status = device->DdkAdd(
          ddk::DeviceAddArgs("bcm2711-gpio").set_proto_id(ZX_PROTOCOL_GPIO_IMPL))) != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Create: DdkAdd failed");
    return status;
  }

  __UNUSED auto* unused = device.release();

  return ZX_OK;
}

zx_status_t Bcm2711Gpio::Init() {

  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::Init: zx_port_create failed %d", status);
    return status;
  }

  fbl::AutoLock al(&irq_lock_);

  uint32_t port_key = 0;
  for (const zx::interrupt& port_interrupt : port_interrupts_) {
    status = port_interrupt.bind(port_, port_key++, ZX_INTERRUPT_BIND);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Bcm2711Gpio::Init: zx_interrupt_bind failed %d", status);
      return status;
    }
  }

  const size_t interrupt_count = BCM2711_GPIO_REG_SIZE * BCM2711_GPIO_INT_REG_NUM;

  fbl::AllocChecker ac;
  gpio_interrupts_ = fbl::Array(new (&ac) zx::interrupt[interrupt_count], interrupt_count);
  if (!ac.check()) {
    zxlogf(ERROR, "Bcm2711Gpio::Init: gpio_interrupts_ alloc failed");
    return ZX_ERR_NO_MEMORY;
  }

  auto cb = [](void* arg) -> int { return reinterpret_cast<Bcm2711Gpio*>(arg)->Thread(); };
  status = thrd_create_with_name(&thread_, cb, this, "bcm2711-gpio-thread");
  if (status != thrd_success) {
    zxlogf(ERROR, "Bcm2711Gpio::Init: thrd_create_with_name failed %d", status);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void Bcm2711Gpio::Bind(const pbus_protocol_t& pbus) {

  gpio_impl_protocol_t gpio_proto = {
      .ops = &gpio_impl_protocol_ops_,
      .ctx = this,
  };

  pbus_register_protocol(&pbus, ZX_PROTOCOL_GPIO_IMPL, &gpio_proto, sizeof(gpio_proto));
}

int Bcm2711Gpio::Thread() {

  while (true) {

    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Bcm2711Gpio::Thread: port wait failed: %d", status);
      return thrd_error;
    }

    fbl::AutoLock al(&irq_lock_);

    if (packet.key > port_interrupts_.size()) {
      zxlogf(WARNING, "Bcm2711Gpio::Thread: received interrupt from invalid port");
      continue;
    }
    if (packet.key == port_interrupts_.size()) {
      zxlogf(INFO, "As370Gpio thread terminating");
      return thrd_success;
    }

    {
      fbl::AutoLock al(&mmio_lock_);

      uint32_t index;
      uint32_t event_reg = mmio_.Read32(BCM2711_GPIO_EDS0);      
      if (packet.key == 0) {
        for (index = 0; index <= BCM2711_GPIO_BANK0_END; index++) {      
          if (event_reg & (1 << index)) {
            uint64_t gpio_offset = index;

            // Notify if interrupt is enabled for the GPIO pin.
            if(gpio_interrupts_[gpio_offset]) {
              status = gpio_interrupts_[gpio_offset].trigger(
                  0, zx::time(packet.interrupt.timestamp));
              if (status != ZX_OK) {
                zxlogf(ERROR, "Bcm2711Gpio::Thread: zx_interrupt_trigger failed %d", status);
              }
              mmio_.Write32(event_reg, BCM2711_GPIO_EDS0);
            }
          }
        }
      } else if (packet.key == 1) {
        for (index = BCM2711_GPIO_BANK0_END + 1; index < BCM2711_GPIO_REG_SIZE; index++) {  
          if (event_reg & (1 << index)) {
            uint64_t gpio_offset = index;

            // Notify if interrupt is enabled for the GPIO pin.
            if(gpio_interrupts_[gpio_offset]) {
              status = gpio_interrupts_[gpio_offset].trigger(
                  0, zx::time(packet.interrupt.timestamp));
              if (status != ZX_OK) {
                zxlogf(ERROR, "Bcm2711Gpio::Thread: zx_interrupt_trigger failed %d", status);
              }
              mmio_.Write32(event_reg, BCM2711_GPIO_EDS0);

            }
          }
        }
        event_reg = mmio_.Read32(BCM2711_GPIO_EDS0 + sizeof(uint32_t));
        for (index = 0; index < BCM2711_GPIO_REG_SIZE; index++) {  
          if (event_reg & (1 << index)) {
            uint64_t gpio_offset = BCM2711_GPIO_REG_SIZE + index;

            // Notify if interrupt is enabled for the GPIO pin.
            if(gpio_interrupts_[gpio_offset]) {
              status = gpio_interrupts_[gpio_offset].trigger(
                  0, zx::time(packet.interrupt.timestamp));
              if (status != ZX_OK) {
                zxlogf(ERROR, "Bcm2711Gpio::Thread: zx_interrupt_trigger failed %d", status);
              }
              mmio_.Write32(event_reg, BCM2711_GPIO_EDS0 + sizeof(uint32_t));
            }
          }
        }  
      } else {
        zxlogf(WARNING, "Bcm2711Gpio::Thread: received interrupt from invalid port");
        continue;
      }
      port_interrupts_[packet.key].ack();  
    }
  }
  return thrd_success;
}

zx_status_t Bcm2711Gpio::GpioImplConfigIn(uint32_t index, uint32_t flags) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplConfigIn: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Set the GPIO as pull-up or pull-down
  uint32_t pull_reg_val;
  switch (flags & GPIO_PULL_MASK) {
    case GPIO_PULL_DOWN:
      pull_reg_val = BCM2711_GPIO_PULL_DOWN;
      break;
    case GPIO_PULL_UP:
      pull_reg_val = BCM2711_GPIO_PULL_UP;
      break;
    default:
      pull_reg_val = BCM2711_GPIO_NO_RESISTOR;
  }

  {
    fbl::AutoLock al(&mmio_lock_);

    mmio_.ModifyBits32(pull_reg_val, (2 * index) % BCM2711_GPIO_REG_SIZE, 2,
        BCM2711_GPIO_PUP_PDN_CNTRL_REG0 + index / (BCM2711_GPIO_REG_SIZE / 2) * sizeof(uint32_t));

    mmio_.ModifyBits32(BCM2711_GPIO_FSEL_INPUT, (index *3) % 30, 3,
        BCM2711_GPIO_FSEL0 + index / 10 * sizeof(uint32_t));
  }

  return ZX_OK;
}

zx_status_t Bcm2711Gpio::GpioImplConfigOut(uint32_t index, uint8_t initial_value) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplConfigOut: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  {
    fbl::AutoLock al(&mmio_lock_);

    uint32_t regval = BCM2711_GPIO_MASK(index);
    if (initial_value) {
      mmio_.Write32(regval, BCM2711_GPIO_SET0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t));
    } else {
      mmio_.Write32(regval, BCM2711_GPIO_CLR0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t));
    }

    mmio_.ModifyBits32(BCM2711_GPIO_FSEL_OUTPUT, (index * 3) % 30, 3,
        BCM2711_GPIO_FSEL0 + index / 10 * sizeof(uint32_t));
  }
  return ZX_OK;
}

// Configure a pin for an alternate function specified by fn
zx_status_t Bcm2711Gpio::GpioImplSetAltFunction(const uint32_t index, const uint64_t fn) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplSetAltFunction: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  if (fn / BCM2711_GPIO_FSEL_ALT_NUM != index ) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplSetAltFunction: pin %u and AltFunction missmatch", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  {
    fbl::AutoLock al(&mmio_lock_);

    uint32_t regval;
    switch (fn % BCM2711_GPIO_FSEL_ALT_NUM ) {
      case 0:
        regval = BCM2711_GPIO_FSEL_ALTFUN0;
        break;
      case 1:
        regval = BCM2711_GPIO_FSEL_ALTFUN1;
        break;
      case 2:
        regval = BCM2711_GPIO_FSEL_ALTFUN2;
        break;
      case 3:
        regval = BCM2711_GPIO_FSEL_ALTFUN3;
        break;
      case 4:
        regval = BCM2711_GPIO_FSEL_ALTFUN4;
        break;
      case 5:
        regval = BCM2711_GPIO_FSEL_ALTFUN5;
        break;
    }
    mmio_.ModifyBits32(regval, (fn / BCM2711_GPIO_FSEL_ALT_NUM * 3) % 30, 3,
        BCM2711_GPIO_FSEL0 + fn / (BCM2711_GPIO_FSEL_ALT_NUM * 10) * sizeof(uint32_t));
  }
  return ZX_OK;
}

zx_status_t Bcm2711Gpio::GpioImplRead(uint32_t index, uint8_t* out_value) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplRead: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  uint32_t regval = 0;
  {
    fbl::AutoLock al(&mmio_lock_);
    regval = mmio_.Read32(BCM2711_GPIO_LEV0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t));
  }

  if (regval & BCM2711_GPIO_MASK(index)) {
    *out_value = 1;
  } else {
    *out_value = 0;
  }

  return ZX_OK;
}

zx_status_t Bcm2711Gpio::GpioImplWrite(uint32_t index, uint8_t value) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplWrite: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  {
    fbl::AutoLock al(&mmio_lock_);

    uint32_t regval = BCM2711_GPIO_MASK(index);
    if (value) {
      mmio_.Write32(regval, BCM2711_GPIO_SET0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t));
    } else {
      mmio_.Write32(regval, BCM2711_GPIO_CLR0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t));
    }
  }
  return ZX_OK;
}

zx_status_t Bcm2711Gpio::GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplWrite: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::AutoLock al(&irq_lock_);

  // Map GPIO banks to ports/IRQs
  uint32_t port;
  if (index <= BCM2711_GPIO_BANK0_END) {
    port = 0;
  } else {
    port = 1;
  }

  // Check necessary IRQs
  if (port + 1 > port_interrupts_.size()) {
    zxlogf(ERROR, "Missing IRQ for GPIO Bank%u", port);
    return ZX_ERR_NO_RESOURCES;
  }

  if (gpio_interrupts_[index]) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplGetInterrupt:"
                  "Interrupt already configured for this pin %u", index);
    return ZX_ERR_ALREADY_EXISTS;
  }

  // Configure GPIO Interrupt EDGE and Polarity
  uint32_t mode_reg = 0;
  uint32_t irq_reg_index = index / BCM2711_GPIO_REG_SIZE; 
  switch (flags & ZX_INTERRUPT_MODE_MASK) {
    case ZX_INTERRUPT_MODE_EDGE_HIGH:
      mode_reg = BCM2711_GPIO_REN0 + irq_reg_index * sizeof(uint32_t);
      break;
    case ZX_INTERRUPT_MODE_EDGE_LOW:
      mode_reg = BCM2711_GPIO_FEN0 + irq_reg_index * sizeof(uint32_t);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_HIGH:
      mode_reg = BCM2711_GPIO_HEN0 + irq_reg_index * sizeof(uint32_t);
      break;
    case ZX_INTERRUPT_MODE_LEVEL_LOW:
      mode_reg = BCM2711_GPIO_LEN0 + irq_reg_index * sizeof(uint32_t);
      break;
    case ZX_INTERRUPT_MODE_EDGE_BOTH:
      return ZX_ERR_NOT_SUPPORTED;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  {
    fbl::AutoLock al(&mmio_lock_);
    mmio_.SetBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, mode_reg);
  }

  // Get virtual interrupts for pin and bookkeeping
  zx::interrupt irq;
  zx_status_t status = zx::interrupt::create(zx::resource(), port, ZX_INTERRUPT_VIRTUAL, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplGetInterrupt: zx::interrupt::create failed %d ", status);
    return status;
  }
  status = irq.duplicate(ZX_RIGHT_SAME_RIGHTS, out_irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplGetInterrupt: interrupt.duplicate failed %d ", status);
    return status;
  }
  // Alloc/assign interrupt to pin
  gpio_interrupts_[index] = std::move(irq);

  return status;
}

zx_status_t Bcm2711Gpio::GpioImplReleaseInterrupt(uint32_t index) {

  fbl::AutoLock al(&irq_lock_);
  if ( !gpio_interrupts_[index] ) {
    return ZX_ERR_BAD_STATE;
  }
  gpio_interrupts_[index].destroy();
  gpio_interrupts_[index].reset();

  {
    fbl::AutoLock al(&mmio_lock_);

    uint32_t irq_reg_index = index / BCM2711_GPIO_REG_SIZE; 
    mmio_.ClearBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, 
                             BCM2711_GPIO_REN0 + irq_reg_index * sizeof(uint32_t));
    mmio_.ClearBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, 
                             BCM2711_GPIO_FEN0 + irq_reg_index * sizeof(uint32_t));
    mmio_.ClearBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, 
                             BCM2711_GPIO_HEN0 + irq_reg_index * sizeof(uint32_t));
    mmio_.ClearBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, 
                             BCM2711_GPIO_LEN0 + irq_reg_index * sizeof(uint32_t));
  }
  return ZX_OK;
}

zx_status_t Bcm2711Gpio::GpioImplSetPolarity(uint32_t index, uint32_t polarity) {

  if (index > BCM2711_GPIO_MAX_PIN) {
    zxlogf(ERROR, "Bcm2711Gpio::GpioImplWrite: pin index out of range %u", index);
    return ZX_ERR_OUT_OF_RANGE;
  }

  //Configure GPIO Interrupt Polarity
  uint32_t mode_reg = 0;
  switch (polarity) {
    case GPIO_POLARITY_LOW:
      mode_reg = BCM2711_GPIO_LEN0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t);
      break;
    case GPIO_POLARITY_HIGH:
      mode_reg = BCM2711_GPIO_HEN0 + index / BCM2711_GPIO_REG_SIZE * sizeof(uint32_t);
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }

  {
    fbl::AutoLock al(&mmio_lock_);
    mmio_.SetBit<uint32_t>(index % BCM2711_GPIO_REG_SIZE, mode_reg);
  } 
  return ZX_OK;
}

void Bcm2711Gpio::Shutdown() {

  fbl::AutoLock al(&irq_lock_);
  zx_port_packet packet = {port_interrupts_.size(), ZX_PKT_TYPE_USER, ZX_OK, {}};
  port_.queue(&packet);
  thrd_join(thread_, nullptr);
}

void Bcm2711Gpio::DdkUnbindNew(ddk::UnbindTxn txn) {
  Shutdown();
  txn.Reply();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Bcm2711Gpio::Create;
  return ops;
}();

}  // namespace gpio

// clang-format off
ZIRCON_DRIVER_BEGIN(Bcm2711_gpio, gpio::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOM),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_BCM_GPIO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_BCM2711),
ZIRCON_DRIVER_END(Bcm2711_gpio)
