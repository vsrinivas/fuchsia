// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
#define GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_

#include <fbl/mutex.h>
#include <hypervisor/io.h>

class Guest;

// Implements the PL011 UART.
class Pl011 : public IoHandler {
 public:
  zx_status_t Init(Guest* guest, uint64_t addr);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  static const size_t kBufferSize = 128;
  mutable fbl::Mutex mutex_;

  uint8_t tx_buffer_[kBufferSize] __TA_GUARDED(mutex_) = {};
  uint16_t tx_offset_ __TA_GUARDED(mutex_) = 0;

  uint16_t control_ __TA_GUARDED(mutex_) = 0;

  void Print(uint8_t ch);
};

#endif  // GARNET_LIB_MACHINA_ARCH_ARM64_PL011_H_
