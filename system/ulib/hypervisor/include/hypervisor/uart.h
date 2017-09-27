// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

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

class IoApic;

typedef struct zx_packet_guest_io zx_packet_guest_io_t;
typedef struct zx_vcpu_io zx_vcpu_io_t;

/* Stores the state of a UART. */
class Uart {
public:
    using InterruptFunc = zx_status_t (*)(zx_handle_t vcpu, uint32_t vector);

    Uart(const IoApic* io_apic);
    Uart(const IoApic* io_apic, InterruptFunc raise_interrupt);

    zx_status_t Init(zx_handle_t guest);

    // Start asynchronous handling of UART.
    zx_status_t StartAsync(zx_handle_t guest);

    zx_status_t Read(uint16_t port, zx_vcpu_io_t* vcpu_io);
    zx_status_t Write(const zx_packet_guest_io_t* io);

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

    // Receive buffer register (RBR).
    uint8_t rx_buffer_ TA_GUARDED(mutex_) = 0;
    // Notify input thread that guest is ready for input.
    cnd_t rx_cnd_;

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
