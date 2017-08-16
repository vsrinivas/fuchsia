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

void uart_init(uart_t* uart, const io_apic_t* io_apic) {
    memset(uart, 0, sizeof(*uart));
    uart->io_apic = io_apic;
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
        mtx_lock((mtx_t*) &uart->mutex);
        vcpu_io->u8 = uart->interrupt_enable;
        mtx_unlock((mtx_t*) &uart->mutex);
        break;
    case UART_INTERRUPT_ID_PORT:
        vcpu_io->access_size = 1;
        mtx_lock((mtx_t*) &uart->mutex);
        vcpu_io->u8 = UART_INTERRUPT_ID_NO_FIFO_MASK & uart->interrupt_id;
        mtx_unlock((mtx_t*) &uart->mutex);
        // Technically, we should always reset the interrupt id register to UART_INTERRUPT_ID_NONE
        // after a read, but this requires us to take a lock on every THR output (to set
        // interrupt_id to UART_INTERRUPT_ID_THR_EMPTY before we fire the interrupt).
        // We aren't too fussed about being perfect here, so instead we will reset it when
        // UART_INTERRUPT_ENABLE_THR_EMPTY is disabled below.
        break;
    case UART_LINE_CONTROL_PORT:
        vcpu_io->access_size = 1;
        mtx_lock((mtx_t*) &uart->mutex);
        vcpu_io->u8 = uart->line_control;
        mtx_unlock((mtx_t*) &uart->mutex);
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

static mx_status_t raise_thr_empty(mx_handle_t vcpu, const io_apic_t* io_apic) {
    static uint32_t vector = 0;
    if (vector == 0) {
        vector = io_apic_redirect(io_apic, X86_INT_UART);
        // UART IRQs overlap with CPU exception handlers, so they need to be
        // remapped. If that hasn't happened yet, don't fire the interrupt - it
        // would be bad.
        if (vector == 0)
            return MX_OK;
    }

    return mx_vcpu_interrupt(vcpu, vector);
}

mx_status_t uart_write(uart_t* uart, mx_handle_t vcpu, const mx_guest_io_t* io) {
    static uint8_t buffer[UART_BUFFER_SIZE] = {};
    static uint16_t offset = 0;
    static bool thr_empty = false;

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
            return raise_thr_empty(vcpu, uart->io_apic);
        return MX_OK;
    case UART_INTERRUPT_ENABLE_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        thr_empty = io->u8 & UART_INTERRUPT_ENABLE_THR_EMPTY;
        mtx_lock(&uart->mutex);
        uart->interrupt_enable = io->u8;
        uart->interrupt_id = thr_empty ? UART_INTERRUPT_ID_THR_EMPTY : UART_INTERRUPT_ID_NONE;
        mtx_unlock(&uart->mutex);
        if (thr_empty)
            return raise_thr_empty(vcpu, uart->io_apic);
        return MX_OK;
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

static mx_status_t uart_handler(mx_handle_t vcpu, mx_guest_packet_t* packet, void* ctx) {
    return uart_write(ctx, vcpu, &packet->io);
}

mx_status_t uart_async(uart_t* uart, mx_handle_t vcpu, mx_handle_t guest) {
    const mx_vaddr_t uart_addr = UART_RECEIVE_PORT;
    const size_t uart_len = UART_SCR_SCRATCH_PORT + 1 - uart_addr;
    return device_async(vcpu, guest, MX_GUEST_TRAP_IO, uart_addr, uart_len, uart_handler, uart);
}
