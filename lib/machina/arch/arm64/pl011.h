// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_

#include <mutex>
#include <zx/socket.h>

#include "garnet/lib/machina/io.h"
#include "garnet/lib/machina/platform_device.h"

namespace machina {

class Guest;

// Implements the PL011 UART.
class Pl011 : public IoHandler, public PlatformDevice {
 public:
  Pl011(zx::socket socket);
  zx_status_t Init(Guest* guest);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // PlatformDevice interface.
  zx_status_t ConfigureZbi(void* zbi_base, size_t zbi_max) const override;
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

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
