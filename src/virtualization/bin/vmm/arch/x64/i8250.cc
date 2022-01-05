// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/i8250.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zbitl/error-string.h>
#include <lib/zbitl/image.h>
#include <stdio.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>

#include "src/virtualization/bin/vmm/arch/x64/i8250_registers.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/zbi.h"

I8250::I8250() : interrupt_id_(kI8250InterruptIdNoInterrupt) {}

zx_status_t I8250::Init(Guest* guest, zx::socket* socket, uint64_t addr,
                        InterruptHandler interrupt_handler, uint32_t irq) {
  socket_ = socket;
  interrupt_handler_ = std::move(interrupt_handler);
  irq_ = irq;
  return guest->CreateMapping(TrapType::PIO_SYNC, addr, kI8250Size, 0, this);
}

zx_status_t I8250::Read(uint64_t addr, IoValue* io) {
  switch (static_cast<I8250Register>(addr)) {
    case I8250Register::INTERRUPT_ENABLE:
      io->access_size = 1;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        io->u8 = interrupt_enable_;
      }
      return ZX_OK;
    case I8250Register::LINE_CONTROL:
      io->access_size = 1;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        io->u8 = line_control_;
      }
      return ZX_OK;
    case I8250Register::LINE_STATUS:
      io->access_size = 1;
      io->u8 = kI8250LineStatusIdle | kI8250LineStatusEmpty;
      return ZX_OK;
    case I8250Register::RECEIVE:
      io->access_size = 1;
      io->u8 = 0;
      return ZX_OK;
    case I8250Register::INTERRUPT_ID:
      io->access_size = 1;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        io->u8 = interrupt_id_;
        interrupt_id_ = kI8250InterruptIdNoInterrupt;
      }
      return ZX_OK;
    case I8250Register::MODEM_CONTROL:
    case I8250Register::MODEM_STATUS... I8250Register::SCRATCH:
      io->access_size = 1;
      io->u8 = 0;
      return ZX_OK;
    default:
      FX_LOGS(ERROR) << "Unhandled I8250 read 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

zx_status_t I8250::Write(uint64_t addr, const IoValue& io) {
  switch (static_cast<I8250Register>(addr)) {
    case I8250Register::TRANSMIT: {
      bool need_interrupt = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < io.access_size; i++) {
          PrintLocked(io.data[i]);
        }
        if (interrupt_enable_ & kI8250InterruptEnableTransmitEmpty) {
          // THR is always empty as soon as we send.
          interrupt_id_ = kI8250InterruptIdTransmitEmpty;
          need_interrupt = true;
        }
      }
      if (need_interrupt) {
        interrupt_handler_(irq_);
      }
      return ZX_OK;
    }
    case I8250Register::INTERRUPT_ENABLE:
      if (io.access_size != 1) {
        return ZX_ERR_IO;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        interrupt_enable_ = io.u8;
      }
      return ZX_OK;
    case I8250Register::LINE_CONTROL:
      if (io.access_size != 1) {
        return ZX_ERR_IO;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        line_control_ = io.u8;
      }
      return ZX_OK;
    case I8250Register::INTERRUPT_ID:
    case I8250Register::MODEM_CONTROL... I8250Register::SCRATCH:
      return ZX_OK;
    default:
      FX_LOGS(ERROR) << "Unhandled I8250 write 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

void I8250::PrintLocked(uint8_t ch) {
  tx_buffer_[tx_offset_++] = ch;
  if (tx_offset_ < kBufferSize && ch != '\r') {
    return;
  }
  size_t actual;
  zx_status_t status = socket_->write(0, tx_buffer_, tx_offset_, &actual);
  if (status != ZX_OK || actual != tx_offset_) {
    FX_LOGS(WARNING) << "I8250 output partial or dropped";
  }
  tx_offset_ = 0;
}

I8250Group::I8250Group(zx::socket socket) : socket_(std::move(socket)) {}

zx_status_t I8250Group::Init(Guest* guest, const I8250::InterruptHandler& interrupt_handler) {
  const struct {
    uint64_t base;
    uint32_t irq;
  } kUarts[kNumUarts] = {
      {kI8250Base0, kI8250Irq0},
      {kI8250Base1, kI8250Irq1},
      {kI8250Base2, kI8250Irq2},
      {kI8250Base3, kI8250Irq3},
  };
  for (size_t i = 0; i < kNumUarts; i++) {
    zx_status_t status =
        uarts_[i].Init(guest, &socket_, kUarts[i].base, interrupt_handler, kUarts[i].irq);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t I8250Group::ConfigureZbi(cpp20::span<std::byte> zbi) const {
  dcfg_simple_pio_t zbi_uart = {
      .base = kI8250Base0,
      .irq = kI8250Irq0,
  };
  zbitl::Image image(zbi);
  return LogIfZbiError(image.Append(
      zbi_header_t{
          .type = ZBI_TYPE_KERNEL_DRIVER,
          .extra = KDRV_I8250_PIO_UART,
      },
      zbitl::AsBytes(&zbi_uart, sizeof(zbi_uart))));
}
