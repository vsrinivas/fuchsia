// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/io.h>
#include <zircon/thread_annotations.h>

class Guest;

// Stores the local APIC state.
class LocalApic : public IoHandler {
public:
    // From Intel Volume 3, Section 10.4.1.: All 32-bit registers should be
    // accessed using 128-bit aligned 32-bit loads or stores. Some processors
    // may support loads and stores of less than 32 bits to some of the APIC
    // registers. This is model specific behavior and is not guaranteed to work
    // on all processors.
    union Register {
        volatile uint32_t u32;
    } __PACKED __ALIGNED(16);

    // Local APIC register map.
    struct Registers {
        Register reserved0[2];

        Register id;      // Read/Write.
        Register version; // Read Only.

        Register reserved1[4];

        Register tpr;       // Read/Write.
        Register apr;       // Read Only.
        Register ppr;       // Read Only.
        Register eoi;       // Write Only.
        Register rrd;       // Read Only.
        Register ldr;       // Read/Write.
        Register dfr;       // Read/Write.
        Register isr[8];    // Read Only.
        Register tmr[8];    // Read Only.
        Register irr[8];    // Read Only.
        Register esr;       // Read Only.

        Register reserved2[6];

        Register lvt_cmci;  // Read/Write.
    } __PACKED;

    LocalApic(zx_handle_t vcpu, uintptr_t apic_addr)
        : vcpu_(vcpu), registers_(reinterpret_cast<Registers*>(apic_addr)) {}

    zx_status_t Init(Guest* guest);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* value) override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

    // Sets the value of the id register.
    void set_id(uint32_t id);
    // Read the value of the LDR register.
    uint32_t ldr() const;
    // Read the value of the DFR register.
    uint32_t dfr() const;

    zx_handle_t vcpu() const { return vcpu_; }

private:
    mutable fbl::Mutex mutex_;
    // VCPU associated with this APIC.
    const zx_handle_t vcpu_;
    // Register accessors.
    Registers* registers_ TA_GUARDED(mutex_);
};
