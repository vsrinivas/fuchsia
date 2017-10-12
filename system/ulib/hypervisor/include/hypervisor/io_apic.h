// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/thread_annotations.h>

class LocalApic;

/* Stores the IO APIC state. */
class IoApic : public IoHandler {
public:
    static constexpr size_t kNumRedirects = 48u;
    static constexpr size_t kNumRedirectOffsets = kNumRedirects * 2;
    static constexpr size_t kMaxLocalApics = 16u;

    // An entry in the redirect table.
    struct RedirectEntry {
        uint32_t upper;
        uint32_t lower;
    };

    zx_status_t Init(Guest* guest);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* value) override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

    // Associate a local APIC with an IO APIC.
    zx_status_t RegisterLocalApic(uint8_t local_apic_id, LocalApic* local_apic);

    // Returns the redirected interrupt vector and target VCPU for the given
    // global IRQ.
    zx_status_t Redirect(uint32_t global_irq, uint8_t* vector, zx_handle_t* vcpu) const;

    // Writes the redirect entry for a global IRQ.
    zx_status_t SetRedirect(uint32_t global_irq, RedirectEntry& redirect);

    // Signals the given global IRQ.
    zx_status_t Interrupt(uint32_t global_irq) const;

private:
    zx_status_t ReadRegister(uint32_t select_register, IoValue* value);
    zx_status_t WriteRegister(uint32_t select_register, const IoValue& value);

    mutable fbl::Mutex mutex_;
    // IO register-select register.
    uint32_t select_ TA_GUARDED(mutex_) = 0;
    // IO APIC identification register.
    uint32_t id_ TA_GUARDED(mutex_) = 0;
    // IO redirection table.
    RedirectEntry redirect_[kNumRedirects] TA_GUARDED(mutex_) = {};
    // Connected local APICs.
    LocalApic* local_apic_[kMaxLocalApics] = {};
};
