// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_

#include <lib/zx/status.h>

#include <mutex>

#include <hwreg/bitfields.h>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// An entry in the IO APIC redirect table.
//
// Bit definitions for the redirect entry. See _Intel I/O Controller Hub
// 10 (ICH10) Family Datasheet (October 2008), Section 13.5_.
struct IoApicRedirectEntry {
  uint64_t raw;

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

// Implements the IO APIC.
//
// See _82093AA (I/O APIC) datasheet_ for high-level details about the APIC,
// and _Intel I/O Controller Hub 10 (ICH10) Family Datasheet (October 2008),
// Section 13.5_ for extensions to the original specification.
class IoApic : public IoHandler, public PlatformDevice {
 public:
  // Callback used when an interrupt is triggered.
  using InterruptCallback = fit::function<zx_status_t(uint64_t mask, uint32_t vector)>;

  static constexpr uint64_t kPhysBase = 0xf8000000;
  static constexpr uint8_t kNumInterrupts = 48u;

  explicit IoApic(Guest* guest);
  IoApic(Guest* guest, InterruptCallback interrupt);

  zx_status_t Init();

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "IO APIC"; }

  // Signals the given global IRQ.
  zx_status_t Interrupt(uint32_t global_irq);

 private:
  // State for global interrupts coming into the IO-APIC.
  //
  // The IO-APIC tracks which vector each IRQ should be routed to (via
  // the IoApicRedirectEntry) and an interrupt has been received while the IRQ
  // was masked.
  struct InputInterrupt {
    IoApicRedirectEntry entry;
    bool pending;
  };

  // An action that needs to be taken by the caller.
  //
  // If std::nullopt, no action is required. If a IoApicRedirectEntry, the caller
  // is responsible for deliverying the given interrupt.
  using Action = std::optional<IoApicRedirectEntry>;

  // Read or write indirect registers directly.
  zx_status_t ReadRegisterLocked(uint8_t select_register, IoValue* value) const
      __TA_REQUIRES(mutex_);
  zx::result<Action> WriteRegisterLocked(uint8_t select_register, const IoValue& value)
      __TA_REQUIRES(mutex_);

  // Read/write the given redirect entry.
  //
  // Each redirect entry consists of two 32-bit words, which can be
  // accessed via RedirectWord::kLower and RedirectWord::kUpper.
  //
  // Writes may require an interrupt to be fired (e.g., if an interrupt
  // was unmasked). This is required to be done by the caller to avoid
  // triggering interrupts inside the IO APIC's lock.
  enum class RedirectBits {
    kLower,  // Access bits [0:31]
    kUpper,  // Access bits [32:63]
  };
  zx_status_t ReadRedirectEntryLocked(uint32_t global_irq, RedirectBits bits, IoValue* result) const
      __TA_REQUIRES(mutex_);
  zx::result<Action> WriteRedirectEntryLocked(uint32_t global_irq, RedirectBits bits,
                                              const IoValue& value) __TA_REQUIRES(mutex_);

  // Deliver an interrupt to the guest according to the given IoApicRedirectEntry.
  zx_status_t DeliverInterrupt(const IoApicRedirectEntry& entry);

  mutable std::mutex mutex_;

  Guest* const guest_;
  const InterruptCallback interrupt_fn_;     // Callback for the IO APIC to trigger an interrupt
  uint8_t select_ __TA_GUARDED(mutex_) = 0;  // IO register-select register.
  uint32_t id_ __TA_GUARDED(mutex_) = 0;     // IO APIC identification register.

  // Input global IRQs.
  InputInterrupt input_interrupts_[kNumInterrupts] __TA_GUARDED(mutex_) = {};
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_IO_APIC_H_
