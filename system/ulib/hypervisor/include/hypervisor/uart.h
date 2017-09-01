// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/types.h>

// clang-format off

/* UART configuration flags. */
#define UART_INTERRUPT_ENABLE_RDA       (1u << 0)
#define UART_INTERRUPT_ENABLE_THR_EMPTY (1u << 1)
#define UART_INTERRUPT_ID_NONE          (1u << 0)
#define UART_INTERRUPT_ID_THR_EMPTY     (1u << 1)
#define UART_INTERRUPT_ID_RDA           (1u << 2)
#define UART_LINE_CONTROL_DIV_LATCH     (1u << 7)
#define UART_STATUS_EMPTY               (1u << 5)
#define UART_STATUS_IDLE                (1u << 6)

#define UART_THR_EMPTY                  (UART_STATUS_IDLE | UART_STATUS_EMPTY)

/* Interrupt vectors. */
#define X86_INT_UART                    4u

// clang-format on

__BEGIN_CDECLS

typedef struct io_apic io_apic_t;
typedef struct mx_packet_guest_io mx_packet_guest_io_t;
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
    // Line status register.
    uint8_t line_status;

    // Raises an interrupt.
    mx_status_t (*raise_interrupt)(mx_handle_t vcpu, uint32_t vector);
} uart_t;

void uart_init(uart_t* uart, const io_apic_t* io_apic);
mx_status_t uart_read(uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t uart_write(uart_t* uart, const mx_packet_guest_io_t* io);

/* Start asynchronous handling of writes to the UART. */
mx_status_t uart_async(uart_t* uart, mx_handle_t vcpu, mx_handle_t guest);

__END_CDECLS
