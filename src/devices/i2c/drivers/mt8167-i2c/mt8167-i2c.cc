// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt8167-i2c.h"

#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/device-protocol/platform-device.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "src/devices/i2c/drivers/mt8167-i2c/mt8167_i2c_bind.h"

//#define TEST_USB_REGS_READ

namespace mt8167_i2c {

constexpr size_t kMaxTransferSize = UINT16_MAX - 1;  // More than enough.
constexpr size_t kHwFifoSize = 8;
constexpr uint32_t kEventCompletion = ZX_USER_SIGNAL_0;
constexpr zx::duration kTimeout = zx::msec(10);
constexpr uint32_t kAltFunctionGpio = 0;
constexpr uint32_t kAltFunctionI2c = 1;

uint32_t Mt8167I2c::I2cImplGetBusCount() { return bus_count_; }

zx_status_t Mt8167I2c::I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
  *out_size = kMaxTransferSize;
  return ZX_OK;
}

zx_status_t Mt8167I2c::I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) {
  // TODO(andresoportus): Support changing frequencies.
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Mt8167I2c::I2cImplTransact(uint32_t id, const i2c_impl_op_t* ops, size_t count) {
  zx_status_t status = ZX_OK;
  if (id >= bus_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto control_reg = ControlReg::Get().ReadFrom(&keys_[id].mmio);
  control_reg.set_ackerr_det_en(1).set_clk_ext_en(1).WriteTo(&keys_[id].mmio);

  for (size_t i = 0; i < count; ++i) {
    if (ops[i].address > 0xFF) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    uint8_t addr = static_cast<uint8_t>(ops[i].address);
    // TODO(andresoportus): Add support for HW transaction (write followed by read).
    status = Transact(ops[i].is_read, id, addr, ops[i].data_buffer, ops[i].data_size, ops[i].stop);
    if (status != ZX_OK && bind_finished_) {
      zxlogf(ERROR, "%s: error in bus id: %u  addr: 0x%X  size: %lu", __func__, id, addr,
             ops[i].data_size);
      Reset(id);
      return status;
    }
  }

  return ZX_OK;
}

int Mt8167I2c::IrqThread() {
  zx_port_packet_t packet;
  while (1) {
    auto status = irq_port_.wait(zx::time::infinite(), &packet);
    zxlogf(DEBUG, "Port key %lu triggered", packet.key);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq_port_.wait failed %d ", __func__, status);
      return status;
    }
    auto id = static_cast<uint32_t>(packet.key);
    ZX_ASSERT(id < keys_.size());
    keys_[id].irq.ack();
    keys_[id].event.signal(0, kEventCompletion);
  }
}

void Mt8167I2c::Reset(uint32_t id) {
  SoftResetReg::Get().ReadFrom(&keys_[id].mmio).set_soft_reset(1).WriteTo(&keys_[id].mmio);
  IntrStatReg::Get().FromValue(0xFFFFFFFF).WriteTo(&keys_[id].mmio);  // Write to clear register.
}

void Mt8167I2c::DataMove(bool is_read, uint32_t id, void* buf, size_t len) {
  uint8_t* p = static_cast<uint8_t*>(buf);
  for (size_t i = 0; i < len; ++i) {
    if (is_read) {
      p[i] = DataPortReg::Get().ReadFrom(&keys_[id].mmio).reg_value();
    } else {
      DataPortReg::Get().FromValue(p[i]).WriteTo(&keys_[id].mmio);
    }
  }
}

zx_status_t Mt8167I2c::Transact(bool is_read, uint32_t id, uint8_t addr, void* buf, size_t len,
                                bool stop) {
  zx_status_t status;

  // TODO(andresoportus): Only stop when stop is set.
  // TODO(andresoportus): Add support for arbitrary sizes.
  if (len > kHwFifoSize) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  uint8_t addr_dir = static_cast<uint8_t>((addr << 1) | is_read);
  FifoAddrClrReg::Get().ReadFrom(&keys_[id].mmio).set_fifo_addr_clr(1).WriteTo(&keys_[id].mmio);
  SlaveAddrReg::Get().ReadFrom(&keys_[id].mmio).set_reg_value(addr_dir).WriteTo(&keys_[id].mmio);
  TransferLenReg::Get().FromValue(static_cast<uint8_t>(len)).WriteTo(&keys_[id].mmio);
  TransacLenReg::Get().FromValue(1).WriteTo(&keys_[id].mmio);  // Single transaction of len bytes.

  IntrStatReg::Get().FromValue(0xFFFFFFFF).WriteTo(&keys_[id].mmio);  // Write to clear register.

  if (!is_read) {
    DataMove(is_read, id, buf, len);
  }

  StartReg::Get().ReadFrom(&keys_[id].mmio).set_start(1).WriteTo(&keys_[id].mmio);
  status = keys_[id].event.wait_one(kEventCompletion, zx::deadline_after(kTimeout), nullptr);
  if (status != ZX_OK) {
    return status;
  }
  status = keys_[id].event.signal(kEventCompletion, 0);
  if (status != ZX_OK) {
    return status;
  }
  if (is_read) {
    DataMove(is_read, id, buf, len);
  }
  auto st = IntrStatReg::Get().ReadFrom(&keys_[id].mmio);
  if (st.arb_lost() || st.hs_nacker() || st.ackerr()) {
    if (bind_finished_) {
      zxlogf(ERROR, "%s: I2C error 0x%X", __func__,
             IntrStatReg::Get().ReadFrom(&keys_[id].mmio).reg_value());
      if (st.ackerr()) {
        zxlogf(ERROR, "%s: No I2C ack reply from peripheral", __func__);
      }
    }
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Mt8167I2c::ShutDown() {
  for (uint32_t id = 0; id < bus_count_; id++) {
    keys_[id].irq.destroy();
  }
  thrd_join(irq_thread_, NULL);
}

void Mt8167I2c::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void Mt8167I2c::DdkRelease() { delete this; }

int Mt8167I2c::TestThread() {
#ifdef TEST_USB_REGS_READ
  constexpr uint32_t bus_id = 2;
  constexpr uint8_t addr = 0x48;
  Reset(bus_id);
  for (uint8_t data_write = 0; data_write < 0xF; ++data_write) {
    uint8_t data_read;
    i2c_impl_op_t ops[] = {
        {.address = addr,
         .data_buffer = &data_write,
         .data_size = 1,
         .is_read = false,
         .stop = false},
        {.address = addr, .data_buffer = &data_read, .data_size = 1, .is_read = true, .stop = true},
    };
    auto status = I2cImplTransact(bus_id, ops, countof(ops));
    if (status == ZX_OK) {
      zxlogf(INFO, "I2C Addr: 0x%02X Reg:0x%02X Value:0x%02X", addr, data_write, data_read);
    }
  }
#endif
  return 0;
}

zx_status_t Mt8167I2c::GetI2cGpios(fbl::Array<ddk::GpioProtocolClient>* out) {
  constexpr size_t kGpioCount = 6;
  fbl::AllocChecker ac;
  fbl::Array<ddk::GpioProtocolClient> gpios(new (&ac) ddk::GpioProtocolClient[kGpioCount],
                                            kGpioCount);
  if (!ac.check()) {
    zxlogf(ERROR, "%s ZX_ERR_NO_MEMORY", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  gpios[0] = ddk::GpioProtocolClient(parent(), "gpio-sda-0");
  gpios[1] = ddk::GpioProtocolClient(parent(), "gpio-scl-0");
  gpios[2] = ddk::GpioProtocolClient(parent(), "gpio-sda-1");
  gpios[3] = ddk::GpioProtocolClient(parent(), "gpio-scl-1");
  gpios[4] = ddk::GpioProtocolClient(parent(), "gpio-sda-2");
  gpios[5] = ddk::GpioProtocolClient(parent(), "gpio-scl-2");
  for (uint32_t i = 0; i < kGpioCount; i++) {
    if (!gpios[i].is_valid()) {
      zxlogf(ERROR, "%s failed to get gpio fragment", __FUNCTION__);
      return ZX_ERR_NO_RESOURCES;
    }
  }

  *out = std::move(gpios);
  return ZX_OK;
}

zx_status_t Mt8167I2c::DoDummyTransactions() {
  fbl::Array<ddk::GpioProtocolClient> gpios;
  zx_status_t status = GetI2cGpios(&gpios);
  if (status != ZX_OK || gpios.size() == 0) {
    return status;
  }

  for (const ddk::GpioProtocolClient& gpio : gpios) {
    gpio.SetAltFunction(kAltFunctionGpio);
  }

  // Do one dummy write on each bus. This works around an issue where the first transaction after
  // enabling the VGP1 regulator gets a NACK error.
  // TODO(fxbug.dev/33282): Figure out a fix for this instead of working around it.
  for (uint32_t id = 0; id < bus_count_; id++) {
    uint8_t byte = 0;
    i2c_impl_op_t ops = {.address = 0x00,
                         .data_buffer = &byte,
                         .data_size = sizeof(byte),
                         .is_read = false,
                         .stop = true};

    I2cImplTransact(id, &ops, 1);
    Reset(id);
  }

  for (const ddk::GpioProtocolClient& gpio : gpios) {
    gpio.SetAltFunction(kAltFunctionI2c);
  }

  return ZX_OK;
}

zx_status_t Mt8167I2c::Bind() {
  zx_status_t status;

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &irq_port_);
  if (status != ZX_OK) {
    return status;
  }
  auto pdev = ddk::PDev::FromFragment(parent());
  pdev_device_info_t info;
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s pdev_get_device_info failed %d", __FUNCTION__, status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  bus_count_ = info.mmio_count - 1;  // Last MMIO is for XO clock.
  if (bus_count_ != MT8167_I2C_CNT) {
    zxlogf(ERROR, "%s wrong I2C count %d", __FUNCTION__, bus_count_);
    return ZX_ERR_INTERNAL;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(bus_count_, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s MapMmio %u failed %d", __FUNCTION__, bus_count_, status);
    return status;
  }
  xo_regs_ = XoRegs(std::move(*mmio));  // Last MMIO is for XO clock.

  for (uint32_t id = 0; id < bus_count_; id++) {
    status = pdev.MapMmio(id, &mmio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s MapMmio %d failed %d", __FUNCTION__, id, status);
      return status;
    }

    zx::event event;
    status = zx::event::create(0, &event);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s zx::event::create failed %d", __FUNCTION__, status);
      return status;
    }
    keys_.push_back({std::move(*mmio), zx::interrupt(), std::move(event)});

    status = pdev.GetInterrupt(id, &keys_[id].irq);
    if (status != ZX_OK) {
      return status;
    }
    status = keys_[id].irq.bind(irq_port_, id, 0);  // id is the port key used.
    if (status != ZX_OK) {
      return status;
    }

    // TODO(andresoportus): Add support for turn on only during transactions?.
    xo_regs_.value().ClockEnable(id, true);

    // TODO(andresoportus): Add support for DMA mode.
  }

  auto thunk = [](void* arg) -> int { return reinterpret_cast<Mt8167I2c*>(arg)->IrqThread(); };
  int rc = thrd_create_with_name(&irq_thread_, thunk, this, "mt8167-i2c");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  status = DoDummyTransactions();
  if (status != ZX_OK) {
    return status;
  }

  bind_finished_ = true;

  status = DdkAdd("mt8167-i2c");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FUNCTION__, status);
    ShutDown();
  }
  return status;
}

zx_status_t Mt8167I2c::Init() {
  auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

#ifdef TEST_USB_REGS_READ
  auto thunk = [](void* arg) -> int { return reinterpret_cast<Mt8167I2c*>(arg)->TestThread(); };
  int rc = thrd_create_with_name(&irq_thread_, thunk, this, "mt8167-i2c-test");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
#endif

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t Mt8167I2c::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<Mt8167I2c>(&ac, parent);
  if (!ac.check()) {
    zxlogf(ERROR, "%s ZX_ERR_NO_MEMORY", __FUNCTION__);
    return ZX_ERR_NO_MEMORY;
  }

  auto status = dev->Bind();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the memory for dev
  auto ptr = dev.release();

  return ptr->Init();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Mt8167I2c::Create;
  return ops;
}();

}  // namespace mt8167_i2c

ZIRCON_DRIVER(mt8167_i2c, mt8167_i2c::driver_ops, "zircon", "0.1");
