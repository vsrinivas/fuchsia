// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <machina/uart.h>

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/guest.h>

// Use an async trap for the first port (TX port) only.
static const uint64_t kI8250AsyncBase = 0;
static const uint64_t kI8250AsyncSize = 1;
static const uint64_t kI8250AsyncOffset = 0;
static const uint64_t kI8250SyncBase = kI8250AsyncSize;
static const uint64_t kI8250SyncSize = I8250_SIZE - kI8250AsyncSize;
static const uint64_t kI8250SyncOffset = kI8250AsyncSize;

// I8250 state flags.
static const uint64_t kI8250LineStatusEmpty = 1u << 5;
static const uint64_t kI8250LineStatusIdle = 1u << 6;

// clang-format off

// I8250 registers.
enum class I8250Register : uint64_t {
    RECEIVE             = 0x0,
    TRANSMIT            = 0x0,
    INTERRUPT_ENABLE    = 0x1,
    INTERRUPT_ID        = 0x2,
    LINE_CONTROL        = 0x3,
    MODEM_CONTROL       = 0x4,
    LINE_STATUS         = 0x5,
    MODEM_STATUS        = 0x6,
    SCRATCH             = 0x7,
};

// clang-format on

zx_status_t I8250::Init(Guest* guest, uint64_t addr) {
    zx_status_t status = guest->CreateMapping(TrapType::PIO_ASYNC, addr + kI8250AsyncBase,
                                              kI8250AsyncSize, kI8250AsyncOffset, this);
    if (status != ZX_OK)
        return status;
    return guest->CreateMapping(TrapType::PIO_SYNC, addr + kI8250SyncBase,
                                kI8250SyncSize, kI8250SyncOffset, this);
}

zx_status_t I8250::Read(uint64_t addr, IoValue* io) const {
    switch (static_cast<I8250Register>(addr)) {
    case I8250Register::INTERRUPT_ENABLE:
        io->access_size = 1;
        {
            fbl::AutoLock lock(&mutex_);
            io->u8 = interrupt_enable_;
        }
        return ZX_OK;
    case I8250Register::LINE_CONTROL:
        io->access_size = 1;
        {
            fbl::AutoLock lock(&mutex_);
            io->u8 = line_control_;
        }
        return ZX_OK;
    case I8250Register::LINE_STATUS:
        io->access_size = 1;
        io->u8 = kI8250LineStatusIdle | kI8250LineStatusEmpty;
        return ZX_OK;
    case I8250Register::RECEIVE:
    case I8250Register::INTERRUPT_ID:
    case I8250Register::MODEM_CONTROL:
    case I8250Register::MODEM_STATUS... I8250Register::SCRATCH:
        io->access_size = 1;
        io->u8 = 0;
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled I8250 address %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

zx_status_t I8250::Write(uint64_t addr, const IoValue& io) {
    switch (static_cast<I8250Register>(addr)) {
    case I8250Register::TRANSMIT:
        for (int i = 0; i < io.access_size; i++) {
            Print(io.data[i]);
        }
        return ZX_OK;
    case I8250Register::INTERRUPT_ENABLE:
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        {
            fbl::AutoLock lock(&mutex_);
            interrupt_enable_ = io.u8;
        }
        return ZX_OK;
    case I8250Register::LINE_CONTROL:
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        {
            fbl::AutoLock lock(&mutex_);
            line_control_ = io.u8;
        }
        return ZX_OK;
    case I8250Register::INTERRUPT_ID:
    case I8250Register::MODEM_CONTROL... I8250Register::SCRATCH:
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled I8250 address %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

void I8250::Print(uint8_t ch) {
    tx_buffer_[tx_offset_++] = ch;
    if (tx_offset_ < kBufferSize && ch != '\r')
        return;
    fprintf(stdout, "%.*s", tx_offset_, tx_buffer_);
    fflush(stdout);
    tx_offset_ = 0;
}
