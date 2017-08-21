// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

// clang-format off

/* UART configuration constants. */
#define UART_BUFFER_SIZE                512u

/* UART configuration masks. */
#define UART_INTERRUPT_ID_NO_FIFO_MASK  BIT_MASK(4)

// clang-format on

void uart_init(uart_t* uart, const io_apic_t* io_apic) {
    memset(uart, 0, sizeof(*uart));
    uart->io_apic = io_apic;
    uart->line_status = UART_THR_EMPTY;
    uart->interrupt_id = UART_INTERRUPT_ID_NONE;
    uart->raise_interrupt = mx_vcpu_interrupt;
}

static mx_status_t maybe_raise_interrupt(uart_t* uart, uint8_t interrupt_id) {
    uint8_t vector = 0;
    mx_handle_t vcpu;
    mx_status_t status = io_apic_redirect(uart->io_apic, X86_INT_UART, &vector, &vcpu);
    if (status != MX_OK)
        return status;

    // UART IRQs overlap with CPU exception handlers, so they need to be
    // remapped. If that hasn't happened yet, don't fire the interrupt - it
    // would be bad.
    if (vector == 0)
        return MX_OK;

    uart->interrupt_id = interrupt_id;
    return uart->raise_interrupt(vcpu, vector);
}

static mx_status_t maybe_raise_thr_empty(uart_t* uart) {
    if (uart->interrupt_enable & UART_INTERRUPT_ENABLE_THR_EMPTY)
        return maybe_raise_interrupt(uart, UART_INTERRUPT_ID_THR_EMPTY);
    return MX_OK;
}

mx_status_t uart_read(uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io) {
    switch (port) {
    case UART_RECEIVE_PORT:
    case UART_MODEM_CONTROL_PORT:
    case UART_MODEM_STATUS_PORT:
    case UART_SCR_SCRATCH_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = 0;
        break;
    case UART_INTERRUPT_ENABLE_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->interrupt_enable;
        mtx_unlock(&uart->mutex);
        break;
    case UART_INTERRUPT_ID_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = UART_INTERRUPT_ID_NO_FIFO_MASK & uart->interrupt_id;

        // Reset THR empty interrupt on IIR (or RBR) access.
        if (uart->interrupt_id & UART_INTERRUPT_ID_THR_EMPTY)
            uart->interrupt_id = UART_INTERRUPT_ID_NONE;

        mtx_unlock(&uart->mutex);
        break;
    case UART_LINE_CONTROL_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->line_control;
        mtx_unlock(&uart->mutex);
        break;
    case UART_LINE_STATUS_PORT:
        vcpu_io->access_size = 1;
        mtx_lock(&uart->mutex);
        vcpu_io->u8 = uart->line_status;
        mtx_unlock(&uart->mutex);
        break;
    default:
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

mx_status_t uart_write(uart_t* uart, const mx_packet_guest_io_t* io) {
    static uint8_t buffer[UART_BUFFER_SIZE] = {};
    static uint16_t offset = 0;

    switch (io->port) {
    case UART_RECEIVE_PORT: {
        for (int i = 0; i < io->access_size; i++) {
            buffer[offset++] = io->data[i];
            if (offset == UART_BUFFER_SIZE || io->data[i] == '\r') {
                printf("%.*s", offset, buffer);
                offset = 0;
            }
        }
        mtx_lock(&uart->mutex);
        uart->line_status = UART_THR_EMPTY;

        // Reset THR empty interrupt on THR write.
        if (uart->interrupt_id & UART_INTERRUPT_ID_THR_EMPTY)
            uart->interrupt_id = UART_INTERRUPT_ID_NONE;

        // TODO(andymutton): Do this asynchronously so that we don't overrun linux's
        // interrupt flood check.
        mx_status_t status = maybe_raise_thr_empty(uart);
        mtx_unlock(&uart->mutex);
        return status;
    }
    case UART_INTERRUPT_ENABLE_PORT: {
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&uart->mutex);
        uart->interrupt_enable = io->u8;
        mx_status_t status = maybe_raise_thr_empty(uart);
        mtx_unlock(&uart->mutex);
        return status;
    }
    case UART_LINE_CONTROL_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&uart->mutex);
        uart->line_control = io->u8;
        mtx_unlock(&uart->mutex);
        return MX_OK;
    case UART_INTERRUPT_ID_PORT:
    case UART_MODEM_CONTROL_PORT ... UART_SCR_SCRATCH_PORT:
        return MX_OK;
    default:
        return MX_ERR_INTERNAL;
    }
}

static mx_status_t uart_handler(mx_handle_t vcpu, mx_port_packet_t* packet, void* ctx) {
    return uart_write(ctx, &packet->guest_io);
}

mx_status_t uart_async(uart_t* uart, mx_handle_t vcpu, mx_handle_t guest) {
    const mx_vaddr_t uart_addr = UART_RECEIVE_PORT;
    const size_t uart_len = UART_SCR_SCRATCH_PORT + 1 - uart_addr;
    return device_async(vcpu, guest, MX_GUEST_TRAP_IO, uart_addr, uart_len, uart_handler, uart);
}
