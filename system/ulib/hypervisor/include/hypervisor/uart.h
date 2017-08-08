// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

typedef struct guest_state guest_state_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;

/* Stores the state of a UART. */
typedef struct uart {
    // Interrupt enable register.
    uint8_t interrupt_enable;
    // Interrupt ID register.
    uint8_t interrupt_id;
    // Line control register.
    uint8_t line_control;
} uart_t;

void uart_init(uart_t* uart);
mx_status_t uart_read(const uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t uart_write(guest_state_t* guest_state, mx_handle_t vcpu, const mx_guest_io_t* io);
