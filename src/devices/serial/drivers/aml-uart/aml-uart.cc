// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-uart.h"

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <string.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include <bits/limits.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddktl/device.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <hwreg/mmio.h>

#include "registers.h"
#include "src/devices/serial/drivers/aml-uart/aml_uart_bind.h"

namespace serial {

constexpr auto kMinBaudRate = 2;

zx_status_t AmlUart::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  auto pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "AmlUart::Create: Could not get pdev");
    return ZX_ERR_NO_RESOURCES;
  }

  serial_port_info_t info;
  size_t actual;
  status =
      device_get_metadata(parent, DEVICE_METADATA_SERIAL_PORT_INFO, &info, sizeof(info), &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_get_metadata failed %d", __func__, status);
    return status;
  }
  if (actual < sizeof(info)) {
    zxlogf(ERROR, "%s: serial_port_info_t metadata too small", __func__);
    return ZX_ERR_INTERNAL;
  }

  std::optional<ddk::MmioBuffer> mmio;
  status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: pdev_map_&mmio__buffer failed %d", __func__, status);
    return status;
  }

  fbl::AllocChecker ac;
  auto* uart = new (&ac) AmlUart(parent, pdev, info, *std::move(mmio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  return uart->Init();
}

zx_status_t AmlUart::Init() {
  auto cleanup = fbl::MakeAutoCall([this]() { DdkRelease(); });

  // Default configuration for the case that serial_impl_config is not called.
  constexpr uint32_t kDefaultBaudRate = 115200;
  constexpr uint32_t kDefaultConfig = SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 | SERIAL_PARITY_NONE;
  SerialImplAsyncConfig(kDefaultBaudRate, kDefaultConfig);
  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_SERIAL_IMPL_ASYNC},
      {BIND_SERIAL_CLASS, 0, serial_port_info_.serial_class},
  };
  auto status = DdkAdd(ddk::DeviceAddArgs("aml-uart").set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkDeviceAdd failed", __func__);
    return status;
  }

  cleanup.cancel();
  return status;
}

uint32_t AmlUart::ReadState() {
  auto status = Status::Get().ReadFrom(&mmio_);
  uint32_t state = 0;
  if (!status.rx_empty()) {
    state |= SERIAL_STATE_READABLE;
  }

  if (!status.tx_full()) {
    state |= SERIAL_STATE_WRITABLE;
  }
  return state;
}

uint32_t AmlUart::ReadStateAndNotify() {
  auto status = Status::Get().ReadFrom(&mmio_);

  uint32_t state = 0;
  if (!status.rx_empty()) {
    state |= SERIAL_STATE_READABLE;
    HandleRX();
  }
  if (!status.tx_full()) {
    state |= SERIAL_STATE_WRITABLE;
    HandleTX();
  }

  return state;
}

int AmlUart::IrqThread() {
  zxlogf(INFO, "%s start", __func__);

  while (1) {
    zx_status_t status;
    status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq.wait() got %d", __func__, status);
      break;
    }
    // This will call the notify_cb if the serial state has changed.
    ReadStateAndNotify();
  }

  return 0;
}

zx_status_t AmlUart::SerialImplAsyncGetInfo(serial_port_info_t* info) {
  memcpy(info, &serial_port_info_, sizeof(*info));
  return ZX_OK;
}

zx_status_t AmlUart::SerialImplAsyncConfig(uint32_t baud_rate, uint32_t flags) {
  // Control register is determined completely by this logic, so start with a clean slate.
  if (baud_rate < kMinBaudRate) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto ctrl = Control::Get().FromValue(0);

  if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
    switch (flags & SERIAL_DATA_BITS_MASK) {
      case SERIAL_DATA_BITS_5:
        ctrl.set_xmit_len(Control::kXmitLength5);
        break;
      case SERIAL_DATA_BITS_6:
        ctrl.set_xmit_len(Control::kXmitLength6);
        break;
      case SERIAL_DATA_BITS_7:
        ctrl.set_xmit_len(Control::kXmitLength7);
        break;
      case SERIAL_DATA_BITS_8:
        ctrl.set_xmit_len(Control::kXmitLength8);
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_STOP_BITS_MASK) {
      case SERIAL_STOP_BITS_1:
        ctrl.set_stop_len(Control::kStopLen1);
        break;
      case SERIAL_STOP_BITS_2:
        ctrl.set_stop_len(Control::kStopLen2);
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_PARITY_MASK) {
      case SERIAL_PARITY_NONE:
        ctrl.set_parity(Control::kParityNone);
        break;
      case SERIAL_PARITY_EVEN:
        ctrl.set_parity(Control::kParityEven);
        break;
      case SERIAL_PARITY_ODD:
        ctrl.set_parity(Control::kParityOdd);
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }

    switch (flags & SERIAL_FLOW_CTRL_MASK) {
      case SERIAL_FLOW_CTRL_NONE:
        ctrl.set_two_wire(1);
        break;
      case SERIAL_FLOW_CTRL_CTS_RTS:
        // CTS/RTS is on by default
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
  }

  // Configure baud rate based on crystal clock speed.
  // See meson_uart_change_speed() in drivers/amlogic/uart/uart/meson_uart.c.
  constexpr uint32_t kCrystalClockSpeed = 24000000;
  uint32_t baud_bits = (kCrystalClockSpeed / 3) / baud_rate - 1;
  if (baud_bits & (~AML_UART_REG5_NEW_BAUD_RATE_MASK)) {
    zxlogf(ERROR, "%s: baud rate %u too large", __func__, baud_rate);
    return ZX_ERR_OUT_OF_RANGE;
  }
  auto baud = Reg5::Get()
                  .FromValue(0)
                  .set_new_baud_rate(baud_bits)
                  .set_use_xtal_clk(1)
                  .set_use_new_baud_rate(1);

  fbl::AutoLock al(&enable_lock_);

  if ((flags & SERIAL_SET_BAUD_RATE_ONLY) == 0) {
    // Invert our RTS if we are we are not enabled and configured for flow control.
    if (!enabled_ && (ctrl.two_wire() == 0)) {
      ctrl.set_inv_rts(1);
    }
    ctrl.WriteTo(&mmio_);
  }

  baud.WriteTo(&mmio_);

  return ZX_OK;
}

void AmlUart::EnableLocked(bool enable) {
  auto ctrl = Control::Get().ReadFrom(&mmio_);

  if (enable) {
    // Reset the port.
    ctrl.set_rst_rx(1).set_rst_tx(1).set_clear_error(1).WriteTo(&mmio_);

    ctrl.set_rst_rx(0).set_rst_tx(0).set_clear_error(0).WriteTo(&mmio_);

    // Enable rx and tx.
    ctrl.set_tx_enable(1)
        .set_rx_enable(1)
        .set_tx_interrupt_enable(1)
        .set_rx_interrupt_enable(1)
        // Clear our RTS.
        .set_inv_rts(0)
        .WriteTo(&mmio_);

    // Set interrupt thresholds.
    // Generate interrupt if TX buffer drops below half full.
    constexpr uint32_t kTransmitIrqCount = 32;
    // Generate interrupt as soon as we receive any data.
    constexpr uint32_t kRecieveIrqCount = 1;
    Misc::Get()
        .FromValue(0)
        .set_xmit_irq_count(kTransmitIrqCount)
        .set_recv_irq_count(kRecieveIrqCount)
        .WriteTo(&mmio_);
  } else {
    ctrl.set_tx_enable(0)
        .set_rx_enable(0)
        // Invert our RTS if we are configured for flow control.
        .set_inv_rts(!ctrl.two_wire())
        .WriteTo(&mmio_);
  }
}

void AmlUart::HandleTXRaceForTest() {
  {
    fbl::AutoLock al(&enable_lock_);
    EnableLocked(true);
  }
  ReadState();
  HandleTX();
  HandleTX();
}

void AmlUart::HandleRXRaceForTest() {
  {
    fbl::AutoLock al(&enable_lock_);
    EnableLocked(true);
  }
  ReadState();
  HandleRX();
  HandleRX();
}

zx_status_t AmlUart::SerialImplAsyncEnable(bool enable) {
  fbl::AutoLock al(&enable_lock_);

  if (enable && !enabled_) {
    zx_status_t status = pdev_.GetInterrupt(0, &irq_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: pdev_get_interrupt failed %d", __func__, status);
      return status;
    }

    EnableLocked(true);

    auto start_thread = [](void* arg) { return static_cast<AmlUart*>(arg)->IrqThread(); };
    int rc = thrd_create_with_name(&irq_thread_, start_thread, this, "aml_uart_irq_thread");
    if (rc != thrd_success) {
      EnableLocked(false);
      return thrd_status_to_zx_status(rc);
    }
  } else if (!enable && enabled_) {
    irq_.destroy();
    thrd_join(irq_thread_, nullptr);
    EnableLocked(false);
  }

  enabled_ = enable;
  return ZX_OK;
}

void AmlUart::SerialImplAsyncReadAsync(serial_impl_async_read_async_callback callback,
                                       void* cookie) {
  fbl::AutoLock lock(&read_lock_);
  if (read_pending_) {
    lock.release();
    callback(cookie, ZX_ERR_NOT_SUPPORTED, nullptr, 0);
    return;
  }
  read_callback_ = callback;
  read_cookie_ = cookie;
  read_pending_ = true;
  lock.release();
  HandleRX();
}

void AmlUart::SerialImplAsyncCancelAll() {
  {
    fbl::AutoLock read_lock(&read_lock_);
    if (read_pending_) {
      read_pending_ = false;
      auto cb = MakeReadCallbackLocked(ZX_ERR_CANCELED, nullptr, 0);
      read_lock.release();
      cb();
    }
  }
  fbl::AutoLock write_lock(&write_lock_);
  if (write_pending_) {
    write_pending_ = false;
    auto cb = MakeWriteCallbackLocked(ZX_ERR_CANCELED);
    write_lock.release();
    cb();
  }
}

// Handles receiviung data into the buffer and calling the read callback when complete.
// Does nothing if read_pending_ is false.
void AmlUart::HandleRX() {
  fbl::AutoLock lock(&read_lock_);
  if (!read_pending_) {
    return;
  }
  unsigned char buf[128];
  size_t length = 128;
  auto* bufptr = static_cast<uint8_t*>(buf);
  const uint8_t* const end = bufptr + length;
  while (bufptr < end && (ReadState() & SERIAL_STATE_READABLE)) {
    uint32_t val = mmio_.Read32(AML_UART_RFIFO);
    *bufptr++ = static_cast<uint8_t>(val);
  }

  const size_t read = reinterpret_cast<uintptr_t>(bufptr) - reinterpret_cast<uintptr_t>(buf);
  if (read == 0) {
    return;
  }
  // Some bytes were read.  The client must queue another read to get any data.
  read_pending_ = false;
  auto cb = MakeReadCallbackLocked(ZX_OK, buf, read);
  lock.release();
  cb();
}

// Handles transmitting the data in write_buffer_ until it is completely written.
// Does nothing if write_pending_ is not true.
void AmlUart::HandleTX() {
  fbl::AutoLock lock(&write_lock_);
  if (!write_pending_) {
    return;
  }
  const auto* bufptr = static_cast<const uint8_t*>(write_buffer_);
  const uint8_t* const end = bufptr + write_size_;
  while (bufptr < end && (ReadState() & SERIAL_STATE_WRITABLE)) {
    mmio_.Write32(*bufptr++, AML_UART_WFIFO);
  }

  const size_t written =
      reinterpret_cast<uintptr_t>(bufptr) - reinterpret_cast<uintptr_t>(write_buffer_);
  write_size_ -= written;
  write_buffer_ += written;
  if (!write_size_) {
    // The write has completed, notify the client.
    write_pending_ = false;
    auto cb = MakeWriteCallbackLocked(ZX_OK);
    lock.release();
    cb();
  }
}

fit::closure AmlUart::MakeReadCallbackLocked(zx_status_t status, void* buf, size_t len) {
  if (read_callback_ == nullptr) {
    return []() {};
  }
  auto callback = [cb = read_callback_, cookie = read_cookie_, status, buf, len]() {
    cb(cookie, status, reinterpret_cast<uint8_t*>(buf), len);
  };
  read_callback_ = nullptr;
  read_cookie_ = nullptr;
  return callback;
}

fit::closure AmlUart::MakeWriteCallbackLocked(zx_status_t status) {
  if (write_callback_ == nullptr) {
    return []() {};
  }
  auto callback = [cb = write_callback_, cookie = write_cookie_, status]() { cb(cookie, status); };
  write_callback_ = nullptr;
  write_cookie_ = nullptr;
  return callback;
}

void AmlUart::SerialImplAsyncWriteAsync(const uint8_t* buf, size_t length,
                                        serial_impl_async_write_async_callback callback,
                                        void* cookie) {
  fbl::AutoLock lock(&write_lock_);
  if (write_pending_) {
    lock.release();
    callback(cookie, ZX_ERR_NOT_SUPPORTED);
    return;
  }
  write_buffer_ = buf;
  write_size_ = length;
  write_callback_ = callback;
  write_cookie_ = cookie;
  write_pending_ = true;
  lock.release();
  HandleTX();
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = AmlUart::Create;
  return ops;
}();

}  // namespace serial

ZIRCON_DRIVER(aml_uart, serial::driver_ops, "zircon", "0.1");
