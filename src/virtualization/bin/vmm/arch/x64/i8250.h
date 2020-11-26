// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_

#include <lib/zx/socket.h>

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// Implements the I8250 UART.
class I8250 : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, zx::socket* socket, uint64_t addr);

  // |IoHandler|
  zx_status_t Read(uint64_t addr, IoValue* io) const override;
  zx_status_t Write(uint64_t addr, const IoValue& io) override;

 private:
  static constexpr size_t kBufferSize = 128;
  mutable std::mutex mutex_;
  zx::socket* socket_ = nullptr;

  uint8_t tx_buffer_[kBufferSize] __TA_GUARDED(mutex_) = {};
  uint16_t tx_offset_ __TA_GUARDED(mutex_) = 0;

  uint8_t interrupt_enable_ __TA_GUARDED(mutex_) = 0;
  uint8_t line_control_ __TA_GUARDED(mutex_) = 0;

  void Print(uint8_t ch);
};

class I8250Group : public PlatformDevice {
 public:
  I8250Group(zx::socket socket);
  zx_status_t Init(Guest* guest);

  // |PlatformDevice|
  zx_status_t ConfigureZbi(fbl::Span<std::byte> zbi) const override;

 private:
  static constexpr size_t kNumUarts = 4;

  zx::socket socket_;
  I8250 uarts_[kNumUarts];
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_
