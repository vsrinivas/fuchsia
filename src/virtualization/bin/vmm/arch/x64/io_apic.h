// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_

#include <mutex>

#include <hwreg/bitfields.h>

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
    uint64_t raw;

    // Bit definitions for the redirect entry. See _Intel I/O Controller Hub
    // 10 (ICH10) Family Datasheet (October 2008), Section 13.5_.
    DEF_SUBFIELD(raw, 63, 56, destination);
    DEF_SUBFIELD(raw, 55, 48, edid);  // Extended Destination ID
    // Bits 47:17 reserved.
    DEF_SUBBIT(raw, 16, mask);
    DEF_SUBBIT(raw, 15, trigger_mode);
    DEF_SUBBIT(raw, 14, remote_irr);
    DEF_SUBBIT(raw, 13, interrupt_input_pin_polarity);
    DEF_SUBBIT(raw, 12, delivery_status);
    DEF_SUBBIT(raw, 11, destination_mode);
    DEF_SUBFIELD(raw, 10, 8, delivery_mode);
    DEF_SUBFIELD(raw, 7, 0, vector);

    // Allow easy reading/writing to the upper/lower 32-bits of the word.
    DEF_SUBFIELD(raw, 63, 32, upper);
    DEF_SUBFIELD(raw, 31, 0, lower);
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
