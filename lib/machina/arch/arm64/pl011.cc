// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/pl011.h"

#include <stdio.h>

#include <fbl/auto_lock.h>

#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"

namespace machina {

// clang-format off

// PL011 registers.
enum class Pl011Register : uint64_t {
    DR      = 0x00,
    FR      = 0x18,
    IBRD    = 0x24,
    FBRD    = 0x28,
    LCR     = 0x2c,
    CR      = 0x30,
    IFLS    = 0x34,
    IMSC    = 0x38,
    ICR     = 0x44,
};

// clang-format on

zx_status_t Pl011::Init(Guest* guest, uint64_t addr) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, addr, kPl011Size, 0, this);
}

zx_status_t Pl011::Read(uint64_t addr, IoValue* value) const {
  switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR: {
      fbl::AutoLock lock(&mutex_);
      value->u16 = control_;
    }
      return ZX_OK;
    case Pl011Register::FR:
    case Pl011Register::IMSC:
      value->u16 = 0;
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled PL011 address read 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

zx_status_t Pl011::Write(uint64_t addr, const IoValue& value) {
  switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR: {
      fbl::AutoLock lock(&mutex_);
      control_ = value.u16;
    }
      return ZX_OK;
    case Pl011Register::DR:
      Print(value.u8);
      return ZX_OK;
    case Pl011Register::IBRD:
    case Pl011Register::FBRD:
    case Pl011Register::ICR:
    case Pl011Register::IFLS:
    case Pl011Register::IMSC:
    case Pl011Register::LCR:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled PL011 address write 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

void Pl011::Print(uint8_t ch) {
  {
    fbl::AutoLock lock(&mutex_);
    tx_buffer_[tx_offset_++] = ch;
    if (tx_offset_ < kBufferSize && ch != '\r') {
      return;
    }
    fprintf(stdout, "%.*s", tx_offset_, tx_buffer_);
    tx_offset_ = 0;
  }
  fflush(stdout);
}

}  // namespace machina
