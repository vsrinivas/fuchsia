// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/i8250.h"

#include <stdio.h>
#include <zircon/boot/image.h>

#include <libzbi/zbi.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/bin/vmm/guest.h"

// I8250 state flags.
static constexpr uint64_t kI8250LineStatusEmpty = 1u << 5;
static constexpr uint64_t kI8250LineStatusIdle = 1u << 6;

static constexpr uint64_t kI8250Base0 = 0x3f8;
static constexpr uint64_t kI8250Base1 = 0x2f8;
static constexpr uint64_t kI8250Base2 = 0x3e8;
static constexpr uint64_t kI8250Base3 = 0x2e8;
static constexpr uint64_t kI8250Size = 0x8;

// clang-format off

// I8250 registers.
enum class I8250Register : uint64_t {
    RECEIVE             = 0x0,
    TRANSMIT            = 0x0,
    INTERRUPT_ENABLE    = 0x1,
    INTERRUPT_ID        = 0x2,
    LINE_CONTROL        = 0x3,
    MODEM_CONTROL       = 0x4,
    LINE_STATUS         = 0x5,
    MODEM_STATUS        = 0x6,
    SCRATCH             = 0x7,
};

// clang-format on

zx_status_t I8250::Init(Guest* guest, zx::socket* socket, uint64_t addr) {
  socket_ = socket;
  return guest->CreateMapping(TrapType::PIO_SYNC, addr, kI8250Size, 0, this);
}

zx_status_t I8250::Read(uint64_t addr, IoValue* io) const {
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
    case I8250Register::INTERRUPT_ID:
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
    case I8250Register::TRANSMIT:
      for (int i = 0; i < io.access_size; i++) {
        Print(io.data[i]);
      }
      return ZX_OK;
    case I8250Register::INTERRUPT_ENABLE:
      if (io.access_size != 1) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        interrupt_enable_ = io.u8;
      }
      return ZX_OK;
    case I8250Register::LINE_CONTROL:
      if (io.access_size != 1) {
        return ZX_ERR_IO_DATA_INTEGRITY;
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

void I8250::Print(uint8_t ch) {
  std::lock_guard<std::mutex> lock(mutex_);
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

zx_status_t I8250Group::Init(Guest* guest) {
  const uint64_t kUartBases[kNumUarts] = {
      kI8250Base0,
      kI8250Base1,
      kI8250Base2,
      kI8250Base3,
  };
  for (size_t i = 0; i < kNumUarts; i++) {
    zx_status_t status = uarts_[i].Init(guest, &socket_, kUartBases[i]);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t I8250Group::ConfigureZbi(void* zbi_base, size_t zbi_max) const {
  zbi_uart_t zbi_uart = {
      .base = kI8250Base0,
      .type = ZBI_UART_PC_PORT,
      .irq = 4,
  };
  zbi_result_t res =
      zbi_append_section(zbi_base, zbi_max, sizeof(zbi_uart), ZBI_TYPE_DEBUG_UART, 0, 0, &zbi_uart);
  return res == ZBI_RESULT_OK ? ZX_OK : ZX_ERR_INTERNAL;
}
