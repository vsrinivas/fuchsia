// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/decode.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>

#define IO_APIC_REDIRECT_OFFSETS    128u

/* Stores the IO APIC state. */
typedef struct io_apic {
    // IO register-select register.
    uint32_t select;
    // IO APIC identification register.
    uint32_t id;
    // IO redirection table offsets.
    uint32_t redirect[IO_APIC_REDIRECT_OFFSETS];
} io_apic_t;

void io_apic_init(io_apic_t* io_apic);

/* Handle memory access to the IO APIC. */
mx_status_t io_apic_handler(io_apic_t* io_apic, const mx_guest_memory_t* memory,
                            const instruction_t* inst);

/* Returns the redirected interrupt vector for the given global one. */
uint8_t io_apic_redirect(const io_apic_t* io_apic, uint8_t global_vector);
