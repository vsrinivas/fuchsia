// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/mutex.h>
#include <hypervisor/decode.h>
#include <zircon/syscalls/port.h>
#include <zircon/thread_annotations.h>

class LocalApic;

/* Stores the IO APIC state. */
class IoApic {
public:
    static constexpr size_t kNumRedirects = 48u;
    static constexpr size_t kNumRedirectOffsets = kNumRedirects * 2;
    static constexpr size_t kMaxLocalApics = 16u;

    // An entry in the redirect table.
    struct RedirectEntry {
        uint32_t upper;
        uint32_t lower;
    };

    // Associate a local APIC with an IO APIC.
    zx_status_t RegisterLocalApic(uint8_t local_apic_id, LocalApic* local_apic);

    // Handle memory access to the IO APIC.
    zx_status_t Handler(const zx_packet_guest_mem_t* mem, const instruction_t* inst);

    // Returns the redirected interrupt vector and target VCPU for the given
    // global IRQ.
    zx_status_t Redirect(uint32_t global_irq, uint8_t* vector, zx_handle_t* vcpu) const;

    // Writes the redirect entry for a global IRQ.
    zx_status_t SetRedirect(uint32_t global_irq, RedirectEntry& redirect);

    // Signals the given global IRQ.
    zx_status_t Interrupt(uint32_t global_irq) const;

private:
    zx_status_t RegisterHandler(const instruction_t* inst);

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

