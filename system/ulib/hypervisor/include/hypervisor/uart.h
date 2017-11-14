// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

// clang-format off

/* Use an async trap for the first port (TX port) only. */
#define UART_ASYNC_BASE                 0
#define UART_ASYNC_SIZE                 1
#define UART_ASYNC_OFFSET               0
#define UART_SYNC_BASE                  UART_ASYNC_SIZE
#define UART_SYNC_SIZE                  (UART_SIZE - UART_ASYNC_SIZE)
#define UART_SYNC_OFFSET                UART_ASYNC_SIZE

/* UART ports. */
#define UART_RECEIVE_PORT               0x0
#define UART_TRANSMIT_PORT              0x0
#define UART_INTERRUPT_ENABLE_PORT      0x1
#define UART_INTERRUPT_ID_PORT          0x2
#define UART_LINE_CONTROL_PORT          0x3
#define UART_MODEM_CONTROL_PORT         0x4
#define UART_LINE_STATUS_PORT           0x5
#define UART_MODEM_STATUS_PORT          0x6
#define UART_SCR_SCRATCH_PORT           0x7
#define UART_SIZE                       0x8

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

class IoApic;

/* Stores the state of a UART. */
class Uart : public IoHandler {
public:
    using InterruptFunc = zx_status_t (*)(zx_handle_t vcpu, uint32_t vector);

    Uart(const IoApic* io_apic);
    Uart(const IoApic* io_apic, InterruptFunc raise_interrupt);

    // Starts processing input using the file streams provided. If a UART is
    // unused then |nullptr| can be provided as the file stream.
    //
    // This method is *not* thread safe and must only be called during startup
    // before VCPU execution begins.
    zx_status_t Start(Guest* guest, uint64_t addr, FILE* input, FILE* output);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* io) override;
    zx_status_t Write(uint64_t addr, const IoValue& io) override;

    zx_status_t FillRx();
    zx_status_t EmptyTx();

    uint8_t interrupt_id() const {
        fbl::AutoLock lock(&mutex_);
        return interrupt_id_;
    }
    void set_interrupt_id(uint8_t interrupt_id) {
        fbl::AutoLock lock(&mutex_);
        interrupt_id_ = interrupt_id;
    }

    uint8_t interrupt_enable() const {
        fbl::AutoLock lock(&mutex_);
        return interrupt_enable_;
    }
    void set_interrupt_enable(uint8_t interrupt_enable) {
        fbl::AutoLock lock(&mutex_);
        interrupt_enable_ = interrupt_enable;
    }

    uint8_t line_status() const {
        fbl::AutoLock lock(&mutex_);
        return line_status_;
    }
    void set_line_status(uint8_t line_status) {
        fbl::AutoLock lock(&mutex_);
        line_status_ = line_status;
    }

    uint8_t line_control() const {
        fbl::AutoLock lock(&mutex_);
        return line_control_;
    }
    void set_line_control(uint8_t line_control) {
        fbl::AutoLock lock(&mutex_);
        line_control_ = line_control;
    }

    uint8_t rx_buffer() const {
        fbl::AutoLock lock(&mutex_);
        return rx_buffer_;
    }
    void set_rx_buffer(uint8_t rx_buffer) {
        fbl::AutoLock lock(&mutex_);
        rx_buffer_ = rx_buffer;
    }

private:
    zx_status_t RaiseNextInterrupt() TA_REQ(mutex_);
    zx_status_t TryRaiseInterrupt(uint8_t interrupt_id) TA_REQ(mutex_);
    bool CanRaiseInterrupt();

    mutable fbl::Mutex mutex_;

    // IO APIC for use with interrupt redirects.
    const IoApic* const io_apic_;

    // Transmit holding register (THR).
    uint8_t tx_buffer_[UART_BUFFER_SIZE] TA_GUARDED(mutex_) = {};
    uint16_t tx_offset_ TA_GUARDED(mutex_) = 0;

    // Notify output thread that guest has output buffered.
    cnd_t tx_cnd_;
    // Notify handler thread that the tx buffer is empty.
    cnd_t tx_empty_cnd_;

    // Receive buffer register (RBR).
    uint8_t rx_buffer_ TA_GUARDED(mutex_) = 0;
    // Notify input thread that guest is ready for input.
    cnd_t rx_cnd_;

    FILE* input_file_ = nullptr;
    FILE* output_file_ = nullptr;

    // Interrupt enable register (IER).
    uint8_t interrupt_enable_ TA_GUARDED(mutex_) = UART_INTERRUPT_ENABLE_NONE;
    // Interrupt ID register (IIR).
    uint8_t interrupt_id_ TA_GUARDED(mutex_) = UART_INTERRUPT_ID_NONE;
    // Line control register (LCR).
    uint8_t line_control_ TA_GUARDED(mutex_) = 0;
    // Line status register (LSR).
    uint8_t line_status_ TA_GUARDED(mutex_) = UART_LINE_STATUS_THR_EMPTY;

    // Raise an interrupt.
    const InterruptFunc raise_interrupt_;
};
