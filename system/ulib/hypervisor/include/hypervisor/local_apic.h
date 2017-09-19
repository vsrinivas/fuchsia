// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/decode.h>
#include <zircon/syscalls/port.h>

/* Local APIC registers are all 128-bit aligned. */
typedef union local_apic_reg {
    uint32_t data[4];
    uint32_t u32;
} __PACKED __ALIGNED(16) local_apic_reg_t;

/* Local APIC register map. */
typedef struct local_apic_regs {
    local_apic_reg_t reserved1[2];

    local_apic_reg_t id;      // Read/Write.
    local_apic_reg_t version; // Read Only.

    local_apic_reg_t reserved2[4];

    local_apic_reg_t tpr;    // Read/Write.
    local_apic_reg_t apr;    // Read Only.
    local_apic_reg_t ppr;    // Read Only.
    local_apic_reg_t eoi;    // Write Only.
    local_apic_reg_t rrd;    // Read Only.
    local_apic_reg_t ldr;    // Read/Write.
    local_apic_reg_t dfr;    // Read/Write.
    local_apic_reg_t isr[8]; // Read Only.
    local_apic_reg_t tmr[8]; // Read Only.
    local_apic_reg_t irr[8]; // Read Only.
    local_apic_reg_t esr;    // Read Only.
} __PACKED local_apic_regs_t;

/* Stores the local APIC state. */
typedef struct local_apic {
    // VCPU associated with this APIC.
    zx_handle_t vcpu;
    union {
        // Address of the local APIC.
        void* apic_addr;
        // Register accessors.
        local_apic_regs_t* regs;
    };
} local_apic_t;

zx_status_t local_apic_handler(local_apic_t* local_apic, const zx_packet_guest_mem_t* mem,
                               instruction_t* inst);
