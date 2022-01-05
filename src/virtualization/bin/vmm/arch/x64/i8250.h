// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/socket.h>

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// Implements the I8250 UART.
class I8250 : public IoHandler {
 public:
  using InterruptHandler = std::function<void(uint32_t)>;

  I8250();
  zx_status_t Init(Guest* guest, zx::socket* socket, uint64_t addr, InterruptHandler interrupt,
                   uint32_t irq);

  // |IoHandler|
  zx_status_t Read(uint64_t addr, IoValue* io) override;
  zx_status_t Write(uint64_t addr, const IoValue& io) override;
  std::string_view Name() const override { return "I8250"; }

 private:
  static constexpr size_t kBufferSize = 128;
  mutable std::mutex mutex_;
  zx::socket* socket_ = nullptr;

  uint8_t tx_buffer_[kBufferSize] __TA_GUARDED(mutex_) = {};
  uint16_t tx_offset_ __TA_GUARDED(mutex_) = 0;

  uint8_t interrupt_enable_ __TA_GUARDED(mutex_) = 0;
  uint8_t interrupt_id_ __TA_GUARDED(mutex_);
  uint8_t line_control_ __TA_GUARDED(mutex_) = 0;

  InterruptHandler interrupt_handler_;
  uint32_t irq_ = 0;

  void PrintLocked(uint8_t ch) __TA_REQUIRES(mutex_);
};

class I8250Group : public PlatformDevice {
 public:
  I8250Group(zx::socket socket);
  zx_status_t Init(Guest* guest, const I8250::InterruptHandler& interrupt);

  // |PlatformDevice|
  zx_status_t ConfigureZbi(cpp20::span<std::byte> zbi) const override;

 private:
  static constexpr size_t kNumUarts = 4;

  zx::socket socket_;
  I8250 uarts_[kNumUarts];
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_I8250_H_
