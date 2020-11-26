// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL011_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL011_H_

#include <lib/zx/socket.h>

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// Implements the PL011 UART.
class Pl011 : public IoHandler, public PlatformDevice {
 public:
  Pl011(zx::socket socket);
  zx_status_t Init(Guest* guest);

  // |IoHandler|
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // |PlatformDevice|
  zx_status_t ConfigureZbi(fbl::Span<std::byte> zbi) const override;
  zx_status_t ConfigureDtb(void* dtb) const override;

 private:
  static constexpr size_t kBufferSize = 128;
  mutable std::mutex mutex_;
  zx::socket socket_;

  uint8_t tx_buffer_[kBufferSize] __TA_GUARDED(mutex_) = {};
  uint16_t tx_offset_ __TA_GUARDED(mutex_) = 0;

  uint16_t control_ __TA_GUARDED(mutex_) = 0;

  void Print(uint8_t ch);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL011_H_
