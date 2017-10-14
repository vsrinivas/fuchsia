// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/uart.h>

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/io_apic.h>

/* UART configuration masks. */
static const uint8_t kUartInterruptIdNoFifoMask = bit_mask<uint8_t>(4);

Uart::Uart(const IoApic* io_apic)
    : Uart(io_apic, &zx_vcpu_interrupt) {}

Uart::Uart(const IoApic* io_apic, InterruptFunc raise_interrupt)
    : io_apic_(io_apic), raise_interrupt_(raise_interrupt) {
    cnd_init(&rx_cnd_);
    cnd_init(&tx_cnd_);
}

zx_status_t Uart::TryRaiseInterrupt(uint8_t interrupt_id) {
    uint8_t vector = 0;
    zx_handle_t vcpu;
    zx_status_t status = io_apic_->Redirect(X86_INT_UART, &vector, &vcpu);
    if (status != ZX_OK)
        return status;

    // UART IRQs overlap with CPU exception handlers, so they need to be
    // remapped. If that hasn't happened yet, don't fire the interrupt - it
    // would be bad.
    if (vector == 0)
        return ZX_OK;

    interrupt_id_ = interrupt_id;
    return raise_interrupt_(vcpu, vector);
}

// Checks whether an interrupt can successfully be raised. This is a
// convenience for the input thread that allows it to delay processing until
// the caller is ready. Others just always call TryRaiseInterrupt and hope.
bool Uart::CanRaiseInterrupt() {
    uint8_t vector = 0;
    zx_handle_t vcpu;
    zx_status_t status = io_apic_->Redirect(X86_INT_UART, &vector, &vcpu);
    return status == ZX_OK && vector != 0;
}

// Determines whether an interrupt needs to be raised and does so if necessary.
// Will not raise an interrupt if the interrupt_enable bit is not set.
zx_status_t Uart::RaiseNextInterrupt() {
    if (interrupt_id_ != UART_INTERRUPT_ID_NONE)
        // Don't wipe out a pending interrupt, just wait.
        return ZX_OK;
    if (interrupt_enable_ & UART_INTERRUPT_ENABLE_RDA &&
        line_status_ & UART_LINE_STATUS_DATA_READY)
        return TryRaiseInterrupt(UART_INTERRUPT_ID_RDA);
    if (interrupt_enable_ & UART_INTERRUPT_ENABLE_THR_EMPTY &&
        line_status_ & UART_LINE_STATUS_THR_EMPTY)
        return TryRaiseInterrupt(UART_INTERRUPT_ID_THR_EMPTY);
    return ZX_OK;
}

zx_status_t Uart::Read(uint64_t port, IoValue* io) {
    switch (port) {
    case UART_MODEM_CONTROL_PORT:
    case UART_MODEM_STATUS_PORT:
    case UART_SCR_SCRATCH_PORT:
        io->access_size = 1;
        io->u8 = 0;
        break;
    case UART_RECEIVE_PORT: {
        io->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        io->u8 = rx_buffer_;
        rx_buffer_ = 0;
        line_status_ = static_cast<uint8_t>(line_status_ & ~UART_LINE_STATUS_DATA_READY);

        // Reset RDA interrupt on RBR read.
        if (interrupt_id_ & UART_INTERRUPT_ID_RDA)
            interrupt_id_ = UART_INTERRUPT_ID_NONE;

        cnd_signal(&rx_cnd_);
        return RaiseNextInterrupt();
    }
    case UART_INTERRUPT_ENABLE_PORT: {
        io->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        io->u8 = interrupt_enable_;
        break;
    }
    case UART_INTERRUPT_ID_PORT: {
        io->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        io->u8 = kUartInterruptIdNoFifoMask & interrupt_id_;

        // Reset THR empty interrupt on IIR read (or THR write).
        if (interrupt_id_ & UART_INTERRUPT_ID_THR_EMPTY)
            interrupt_id_ = UART_INTERRUPT_ID_NONE;
        break;
    }
    case UART_LINE_CONTROL_PORT: {
        io->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        io->u8 = line_control_;
        break;
    }
    case UART_LINE_STATUS_PORT: {
        io->access_size = 1;
        fbl::AutoLock lock(&mutex_);
        io->u8 = line_status_;
        break;
    }
    default:
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t Uart::Write(uint64_t port, const IoValue& io) {
    switch (port) {
    case UART_TRANSMIT_PORT: {
        fbl::AutoLock lock(&mutex_);
        if (line_control_ & UART_LINE_CONTROL_DIV_LATCH)
            // Ignore writes when divisor latch is enabled.
            return (io.access_size != 1) ? ZX_ERR_IO_DATA_INTEGRITY : ZX_OK;

        for (int i = 0; i < io.access_size; i++) {
            while (tx_offset_ >= sizeof(tx_buffer_)) {
                cnd_wait(&tx_empty_cnd_, mutex_.GetInternal());
            }
            tx_buffer_[tx_offset_++] = io.data[i];
        }

        line_status_ |= UART_LINE_STATUS_THR_EMPTY;

        // Reset THR empty interrupt on THR write.
        if (interrupt_id_ & UART_INTERRUPT_ID_THR_EMPTY)
            interrupt_id_ = UART_INTERRUPT_ID_NONE;

        cnd_signal(&tx_cnd_);
        return RaiseNextInterrupt();
    }
    case UART_INTERRUPT_ENABLE_PORT: {
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        fbl::AutoLock lock(&mutex_);
        // Ignore writes when divisor latch is enabled.
        if (line_control_ & UART_LINE_CONTROL_DIV_LATCH)
            return ZX_OK;

        interrupt_enable_ = io.u8;
        return RaiseNextInterrupt();
    }
    case UART_LINE_CONTROL_PORT: {
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        fbl::AutoLock lock(&mutex_);
        line_control_ = io.u8;
        return ZX_OK;
    }
    case UART_INTERRUPT_ID_PORT:
    case UART_MODEM_CONTROL_PORT... UART_SCR_SCRATCH_PORT:
        return ZX_OK;
    default:
        return ZX_ERR_INTERNAL;
    }
}

static int uart_empty_tx(void* arg) {
    return reinterpret_cast<Uart*>(arg)->EmptyTx();
}

zx_status_t Uart::EmptyTx() {
    while (true) {
        {
            fbl::AutoLock lock(&mutex_);
            while (tx_offset_ == 0) {
                cnd_wait(&tx_cnd_, mutex_.GetInternal());
            }

            printf("%.*s", tx_offset_, tx_buffer_);
            tx_offset_ = 0;
            cnd_signal(&tx_empty_cnd_);
        }

        if (fflush(stdout) == EOF) {
            fprintf(stderr, "Stopped processing UART output\n");
            break;
        }
    }
    return ZX_ERR_INTERNAL;
}

static int uart_fill_rx(void* arg) {
    return reinterpret_cast<Uart*>(arg)->FillRx();
}

zx_status_t Uart::FillRx() {
    zx_status_t status;
    do {
        {
            fbl::AutoLock lock(&mutex_);
            // Wait for a signal that the line is clear.
            // The locking here is okay, because we yield when we wait.
            while (!CanRaiseInterrupt() && line_status_ & UART_LINE_STATUS_DATA_READY)
                cnd_wait(&rx_cnd_, mutex_.GetInternal());
        }

        int pending_char = getchar();
        if (pending_char == '\b')
            // Replace BS with DEL to make Linux happy.
            // TODO(andymutton): Better input handling / terminal emulation.
            pending_char = 0x7f;

        if (pending_char == EOF)
            status = ZX_ERR_PEER_CLOSED;
        else {
            fbl::AutoLock lock(&mutex_);
            rx_buffer_ = static_cast<uint8_t>(pending_char);
            line_status_ |= UART_LINE_STATUS_DATA_READY;
            status = RaiseNextInterrupt();
        }
    } while (status == ZX_OK);
    fprintf(stderr, "Stopped processing UART input (%d)\n", status);
    return status;
}

zx_status_t Uart::Start(Guest* guest) {
    zx_status_t status;
    status = guest->CreateMapping(TrapType::PIO_ASYNC, UART_ASYNC_BASE, UART_ASYNC_SIZE,
                                  UART_ASYNC_OFFSET, this);
    if (status != ZX_OK)
        return status;
    status = guest->CreateMapping(TrapType::PIO_SYNC, UART_SYNC_BASE, UART_SYNC_SIZE,
                                  UART_SYNC_OFFSET, this);
    if (status != ZX_OK)
        return status;

    thrd_t uart_input_thread;
    int ret = thrd_create(&uart_input_thread, uart_fill_rx, this);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create UART input thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }
    ret = thrd_detach(uart_input_thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach UART input thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    thrd_t uart_output_thread;
    ret = thrd_create(&uart_output_thread, uart_empty_tx, this);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create UART output thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    ret = thrd_detach(uart_output_thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach UART output thread %d\n", ret);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}
