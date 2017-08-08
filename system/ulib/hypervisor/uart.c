// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <hypervisor/bits.h>
#include <hypervisor/ports.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

/* UART configuration constants. */
#define UART_BUFFER_SIZE                512u

/* UART configuration flags. */
#define UART_INTERRUPT_ENABLE_THR_EMPTY (1u << 1)
#define UART_INTERRUPT_ID_NONE          (1u << 0)
#define UART_INTERRUPT_ID_THR_EMPTY     (1u << 1)
#define UART_STATUS_EMPTY               (1u << 5)
#define UART_STATUS_IDLE                (1u << 6)

/* UART configuration masks. */
#define UART_INTERRUPT_ID_NO_FIFO_MASK  BIT_MASK(4)

/* Interrupt vectors. */
#define X86_INT_UART                    4u

void uart_init(uart_t* uart) {
    memset(uart, 0, sizeof(*uart));
}

mx_status_t uart_read(const uart_t* uart, uint16_t port, mx_vcpu_io_t* vcpu_io) {
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
        vcpu_io->u8 = uart->interrupt_enable;
        break;
    case UART_INTERRUPT_ID_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = UART_INTERRUPT_ID_NO_FIFO_MASK & uart->interrupt_id;
        // Technically, we should always reset the interrupt id register to UART_INTERRUPT_ID_NONE
        // after a read, but this requires us to take a lock on every THR output (to set
        // interrupt_id to UART_INTERRUPT_ID_THR_EMPTY before we fire the interrupt).
        // We aren't too fussed about being perfect here, so instead we will reset it when
        // UART_INTERRUPT_ENABLE_THR_EMPTY is disabled below.
        break;
    case UART_LINE_CONTROL_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = uart->line_control;
        break;
    case UART_LINE_STATUS_PORT:
        vcpu_io->access_size = 1;
        vcpu_io->u8 = UART_STATUS_IDLE | UART_STATUS_EMPTY;
        break;
    default:
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

static mx_status_t raise_thr_empty(mx_handle_t vcpu, guest_state_t* guest_state) {
    static uint32_t interrupt = 0;
    if (interrupt == 0) {
        // Lock concurrent access to io_apic_state.
        mtx_lock(&guest_state->mutex);
        interrupt = irq_redirect(&guest_state->io_apic_state, X86_INT_UART);
        mtx_unlock(&guest_state->mutex);

        // UART IRQs overlap with CPU exception handlers, so they need to be remapped.
        // If that hasn't happened yet, don't fire the interrupt - it would be bad.
        if (interrupt == 0) {
            return MX_OK;
        }
    }

    return mx_vcpu_interrupt(vcpu, interrupt);
}

mx_status_t uart_write(guest_state_t* guest_state, mx_handle_t vcpu, const mx_guest_io_t* io) {
    static uint8_t buffer[UART_BUFFER_SIZE] = {};
    static uint16_t offset = 0;
    static bool thr_empty = false;

    uart_t* uart = guest_state->uart;

    switch (io->port) {
    case UART_RECEIVE_PORT:
        for (int i = 0; i < io->access_size; i++) {
            buffer[offset++] = io->data[i];
            if (offset == UART_BUFFER_SIZE || io->data[i] == '\r') {
                printf("%.*s", offset, buffer);
                offset = 0;
            }
        }
        if (thr_empty)
            return raise_thr_empty(vcpu, guest_state);
        break;
    case UART_INTERRUPT_ENABLE_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        thr_empty = io->u8 & UART_INTERRUPT_ENABLE_THR_EMPTY;
        mtx_lock(&guest_state->mutex);
        uart->interrupt_enable = io->u8;
        uart->interrupt_id = thr_empty ? UART_INTERRUPT_ID_THR_EMPTY : UART_INTERRUPT_ID_NONE;
        mtx_unlock(&guest_state->mutex);
        if (thr_empty)
            return raise_thr_empty(vcpu, guest_state);
        break;
    case UART_LINE_CONTROL_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(&guest_state->mutex);
        uart->line_control = io->u8;
        mtx_unlock(&guest_state->mutex);
        break;
    case UART_INTERRUPT_ID_PORT:
    case UART_MODEM_CONTROL_PORT ... UART_SCR_SCRATCH_PORT:
        break;
    default:
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}
