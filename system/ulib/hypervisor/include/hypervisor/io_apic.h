// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <hypervisor/decode.h>
#include <magenta/syscalls/port.h>

#define IO_APIC_REDIRECTS 48u
#define IO_APIC_REDIRECT_OFFSETS (IO_APIC_REDIRECTS * 2)
#define IO_APIC_MAX_LOCAL_APICS 16u

__BEGIN_CDECLS

typedef struct local_apic local_apic_t;

/* Stores the IO APIC state. */
typedef struct io_apic {
    mtx_t mutex;
    // IO register-select register.
    uint32_t select;
    // IO APIC identification register.
    uint32_t id;
    // IO redirection table offsets.
    uint32_t redirect[IO_APIC_REDIRECT_OFFSETS];
    // Connected local APICs.
    local_apic_t* local_apic[IO_APIC_MAX_LOCAL_APICS];
} io_apic_t;

void io_apic_init(io_apic_t* io_apic);

/* Associate a local APIC with an IO APIC. */
mx_status_t io_apic_register_local_apic(io_apic_t* io_apic, uint8_t local_apic_id,
                                        local_apic_t* local_apic);

/* Handle memory access to the IO APIC. */
mx_status_t io_apic_handler(io_apic_t* io_apic, const mx_packet_guest_mem_t* mem,
                            const instruction_t* inst);

/* Returns the redirected interrupt vector and target VCPU for the given
 * global IRQ.
 */
mx_status_t io_apic_redirect(const io_apic_t* io_apic, uint32_t global_irq, uint8_t* vector,
                             mx_handle_t* vcpu);

/* Signals the given global IRQ. */
mx_status_t io_apic_interrupt(const io_apic_t* io_apic, uint32_t global_irq);

__END_CDECLS
