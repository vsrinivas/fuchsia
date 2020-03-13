// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_PLATFORM_INTERRUPT_H
#define ZIRCON_PLATFORM_INTERRUPT_H

#include <lib/zx/clock.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>

#include <utility>

#include <ddk/device.h>
#include <ddk/protocol/pci.h>

#include "platform_interrupt.h"

namespace magma {

class ZirconPlatformInterrupt : public PlatformInterrupt {
 public:
  ZirconPlatformInterrupt(zx::handle interrupt_handle) : handle_(std::move(interrupt_handle)) {
    DASSERT(handle_.get() != ZX_HANDLE_INVALID);
  }

  void Signal() override { zx_interrupt_destroy(handle_.get()); }

  bool Wait() override {
    zx_time_t time;
    zx_status_t status = zx_interrupt_wait(handle_.get(), &time);
    timestamp_ = zx::time(time);
    if (status != ZX_OK)
      return DRETF(false, "zx_irq_wait failed (%d)", status);
    return true;
  }

  void Complete() override {}

  uint64_t GetMicrosecondsSinceLastInterrupt() override {
    return (zx::clock::get_monotonic() - timestamp_).to_usecs();
  }

 private:
  zx::handle handle_;
  zx::time timestamp_;
};

}  // namespace magma

#endif  // ZIRCON_PLATFORM_INTERRUPT_H
