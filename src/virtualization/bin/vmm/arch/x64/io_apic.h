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
//
// See _82093AA (I/O APIC) datasheet_ for high-level details about the APIC,
// and _Intel I/O Controller Hub 10 (ICH10) Family Datasheet (October 2008),
// Section 13.5_ for extensions to the original specification.
class IoApic : public IoHandler, public PlatformDevice {
 public:
  static constexpr uint64_t kPhysBase = 0xf8000000;
  static constexpr uint8_t kNumRedirects = 48u;
  static constexpr uint8_t kNumRedirectOffsets = kNumRedirects * 2;

  // An entry in the redirect table.
  struct RedirectEntry {
    uint32_t upper;
    uint32_t lower;
  };

  IoApic(Guest* guest);

  zx_status_t Init();

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "IO APIC"; }

  // Writes the redirect entry for a global IRQ.
  zx_status_t SetRedirect(uint32_t global_irq, RedirectEntry& redirect);

  // Signals the given global IRQ.
  zx_status_t Interrupt(uint32_t global_irq);

  // Read or write indirect registers directly. Exposed for testing.
  zx_status_t ReadRegister(uint8_t select_register, IoValue* value) const;
  zx_status_t WriteRegister(uint8_t select_register, const IoValue& value);

 private:
  Guest* guest_;

  mutable std::mutex mutex_;
  // IO register-select register.
  uint8_t select_ __TA_GUARDED(mutex_) = 0;
  // IO APIC identification register.
  uint32_t id_ __TA_GUARDED(mutex_) = 0;
  // IO redirection table.
  RedirectEntry redirect_[kNumRedirects] __TA_GUARDED(mutex_) = {};
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
