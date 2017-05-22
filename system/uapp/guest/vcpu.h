// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/types.h>

#define IO_APIC_REDIRECT_OFFSETS    0x36
#define IO_BUFFER_SIZE              512u

/* Stores the local APIC state across VM exits. */
typedef struct local_apic_state {
    // Address of the local APIC.
    void* apic_addr;
    // VMO representing the local APIC.
    mx_handle_t apic_mem;
} local_apic_state_t;

/* Stores the IO APIC state across VM exits. */
typedef struct io_apic_state {
    // IO register-select register.
    uint32_t select;
    // IO APIC identification register.
    uint32_t id;
    // IO redirection table offsets.
    uint32_t redirect[IO_APIC_REDIRECT_OFFSETS];
} io_apic_state_t;

/* Stores the IO port state across VM exits. */
typedef struct io_port_state {
    // Buffer containing the output.
    uint8_t buffer[IO_BUFFER_SIZE];
    // Write position within the buffer.
    uint16_t offset;
} io_port_state_t;

typedef struct guest_state {
    mtx_t mutex;
    io_apic_state_t io_apic_state;
    io_port_state_t io_port_state;
} guest_state_t;

typedef struct vcpu_context {
    mx_handle_t guest;
    mx_handle_t vcpu_fifo;

    local_apic_state_t local_apic_state;
    guest_state_t* guest_state;
} vcpu_context_t;

mx_status_t vcpu_loop(vcpu_context_t* context);
