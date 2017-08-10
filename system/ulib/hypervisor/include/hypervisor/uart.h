// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/types.h>

typedef struct io_apic io_apic_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;

/* Stores the state of a UART. */
typedef struct uart {
    mtx_t mutex;
    // IO APIC for use with interrupt redirects.
    const io_apic_t* io_apic;
    // Interrupt enable register.
    uint8_t interrupt_enable;
    // Interrupt ID register.
    uint8_t interrupt_id;
    // Line control register.
    uint8_t line_control;
} uart_t;

void uart_init(uart_t* uart, const io_apic_t* io_apic);
mx_status_t uart_read(const uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t uart_write(uart_t* uart, mx_handle_t vcpu, const mx_guest_io_t* io);

/* Start asynchronous handling of writes to the UART. */
mx_status_t uart_async(uart_t* uart, mx_handle_t vcpu, mx_handle_t guest);
