// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// Implements the IO APIC.
class IoApic : public IoHandler, public PlatformDevice {
 public:
  static constexpr uint64_t kPhysBase = 0xf8000000;
  static constexpr size_t kNumRedirects = 48u;
  static constexpr size_t kNumRedirectOffsets = kNumRedirects * 2;

  // An entry in the redirect table.
  struct RedirectEntry {
    uint32_t upper;
    uint32_t lower;
  };

  IoApic(Guest* guest);

  zx_status_t Init();

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // Writes the redirect entry for a global IRQ.
  zx_status_t SetRedirect(uint32_t global_irq, RedirectEntry& redirect);

  // Signals the given global IRQ.
  zx_status_t Interrupt(uint32_t global_irq);

 private:
  Guest* guest_;

  mutable std::mutex mutex_;
  // IO register-select register.
  uint32_t select_ __TA_GUARDED(mutex_) = 0;
  // IO APIC identification register.
  uint32_t id_ __TA_GUARDED(mutex_) = 0;
  // IO redirection table.
  RedirectEntry redirect_[kNumRedirects] __TA_GUARDED(mutex_) = {};

  zx_status_t ReadRegister(uint32_t select_register, IoValue* value) const;
  zx_status_t WriteRegister(uint32_t select_register, const IoValue& value);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
