// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_IO_APIC_H_
#define GARNET_LIB_MACHINA_ARCH_X86_IO_APIC_H_

#include <fbl/mutex.h>
#include <hypervisor/io.h>
#include <zircon/compiler.h>

class Guest;
class LocalApic;

namespace machina {

// Implements the IO APIC.
class IoApic : public IoHandler {
 public:
  static constexpr size_t kNumRedirects = 48u;
  static constexpr size_t kNumRedirectOffsets = kNumRedirects * 2;

  // An entry in the redirect table.
  struct RedirectEntry {
    uint32_t upper;
    uint32_t lower;
  };

  zx_status_t Init(Guest* guest);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // Associate a local APIC with an IO APIC.
  zx_status_t RegisterLocalApic(uint8_t local_apic_id, LocalApic* local_apic);

  // Writes the redirect entry for a global IRQ.
  zx_status_t SetRedirect(uint32_t global_irq, RedirectEntry& redirect);

  // Signals the given global IRQ.
  zx_status_t Interrupt(uint32_t global_irq);

 private:
  static constexpr size_t kMaxLocalApics = 16u;

  mutable fbl::Mutex mutex_;
  // IO register-select register.
  uint32_t select_ __TA_GUARDED(mutex_) = 0;
  // IO APIC identification register.
  uint32_t id_ __TA_GUARDED(mutex_) = 0;
  // IO redirection table.
  RedirectEntry redirect_[kNumRedirects] __TA_GUARDED(mutex_) = {};
  // Connected local APICs.
  LocalApic* local_apics_[kMaxLocalApics] = {};

  zx_status_t ReadRegister(uint32_t select_register, IoValue* value) const;
  zx_status_t WriteRegister(uint32_t select_register, const IoValue& value);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_IO_APIC_H_
