// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <magenta/types.h>

// clang-format off

/* UART state flags. */
#define UART_INTERRUPT_ENABLE_NONE      0
#define UART_INTERRUPT_ENABLE_RDA       (1u << 0)
#define UART_INTERRUPT_ENABLE_THR_EMPTY (1u << 1)
#define UART_INTERRUPT_ID_NONE          (1u << 0)
#define UART_INTERRUPT_ID_THR_EMPTY     (1u << 1)
#define UART_INTERRUPT_ID_RDA           (1u << 2)
#define UART_LINE_CONTROL_DIV_LATCH     (1u << 7)
#define UART_LINE_STATUS_DATA_READY     (1u << 0)
#define UART_LINE_STATUS_EMPTY          (1u << 5)
#define UART_LINE_STATUS_IDLE           (1u << 6)
#define UART_LINE_STATUS_THR_EMPTY      (UART_LINE_STATUS_IDLE | UART_LINE_STATUS_EMPTY)

/* Interrupt vectors. */
#define X86_INT_UART                    4u

/* UART configuration constants. */
#define UART_BUFFER_SIZE                512u

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

    // Transmit holding register (THR).
    uint8_t tx_buffer[UART_BUFFER_SIZE];
    uint16_t tx_offset;
    // Notify output thread that guest has output buffered.
    cnd_t tx_cnd;

    // Receive buffer register (RBR).
    uint8_t rx_buffer;
    // Notify input thread that guest is ready for input.
    cnd_t rx_cnd;

    // Interrupt enable register (IER).
    uint8_t interrupt_enable;
    // Interrupt ID register (IIR).
    uint8_t interrupt_id;
    // Line control register (LCR).
    uint8_t line_control;
    // Line status register (LSR).
    uint8_t line_status;

    // Raise an interrupt.
    mx_status_t (*raise_interrupt)(mx_handle_t vcpu, uint32_t vector);
} uart_t;

void uart_init(uart_t* uart, const io_apic_t* io_apic);
mx_status_t uart_read(uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io);
mx_status_t uart_write(uart_t* uart, const mx_packet_guest_io_t* io);

/* Start asynchronous handling of UART. */
mx_status_t uart_async(uart_t* uart, mx_handle_t guest);

__END_CDECLS
