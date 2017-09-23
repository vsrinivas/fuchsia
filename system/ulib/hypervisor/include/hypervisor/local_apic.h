// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/mutex.h>
#include <hypervisor/decode.h>
#include <zircon/syscalls/port.h>
#include <zircon/thread_annotations.h>


/* Stores the local APIC state. */
class LocalApic {
public:
    /* Local APIC registers are all 128-bit aligned. */
    union Register {
        uint32_t data[4];
        uint32_t u32;
    } __PACKED __ALIGNED(16);

    /* Local APIC register map. */
    struct Registers {
        Register reserved1[2];

        Register id;      // Read/Write.
        Register version; // Read Only.

        Register reserved2[4];

        Register tpr;    // Read/Write.
        Register apr;    // Read Only.
        Register ppr;    // Read Only.
        Register eoi;    // Write Only.
        Register rrd;    // Read Only.
        Register ldr;    // Read/Write.
        Register dfr;    // Read/Write.
        Register isr[8]; // Read Only.
        Register tmr[8]; // Read Only.
        Register irr[8]; // Read Only.
        Register esr;    // Read Only.
    } __PACKED;

    LocalApic(zx_handle_t vcpu, uintptr_t apic_addr)
        : vcpu_(vcpu),
          regs_(reinterpret_cast<Registers*>(apic_addr)) {}

    zx_status_t Handler(const zx_packet_guest_mem_t* mem, instruction_t* inst);

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
    const zx_handle_t vcpu_ = ZX_HANDLE_INVALID;
    // Register accessors.
    Registers* regs_ TA_GUARDED(mutex_);
};

