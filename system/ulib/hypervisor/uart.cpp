// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/uart.h>

#include <stdio.h>

#include <hypervisor/address.h>
#include <hypervisor/guest.h>

// clang-format off

// UART ports.
static const uint64_t kUartReceivePort          = 0x0;
static const uint64_t kUartTransmitPort         = 0x0;
static const uint64_t kUartInterruptEnablePort  = 0x1;
static const uint64_t kUartInterruptIdPort      = 0x2;
static const uint64_t kUartLineControlPort      = 0x3;
static const uint64_t kUartModemControlPort     = 0x4;
static const uint64_t kUartLineStatusPort       = 0x5;
static const uint64_t kUartModemStatusPort      = 0x6;
static const uint64_t kUartScrScratchPort       = 0x7;

// Use an async trap for the first port (TX port) only.
static const uint64_t kUartAsyncBase            = 0;
static const uint64_t kUartAsyncSize            = 1;
static const uint64_t kUartAsyncOffset          = 0;
static const uint64_t kUartSyncBase             = kUartAsyncSize;
static const uint64_t kUartSyncSize             = UART_SIZE - kUartAsyncSize;
static const uint64_t kUartSyncOffset           = kUartAsyncSize;

// UART state flags.
static const uint64_t kUartLineStatusEmpty      = 1u << 5;
static const uint64_t kUartLineStatusIdle       = 1u << 6;

// clang-format on

zx_status_t Uart::Init(Guest* guest, uint64_t addr) {
    zx_status_t status = guest->CreateMapping(TrapType::PIO_ASYNC, addr + kUartAsyncBase,
                                              kUartAsyncSize, kUartAsyncOffset, this);
    if (status != ZX_OK)
        return status;
    return guest->CreateMapping(TrapType::PIO_SYNC, addr + kUartSyncBase,
                                kUartSyncSize, kUartSyncOffset, this);
}

zx_status_t Uart::Read(uint64_t addr, IoValue* io) {
    switch (addr) {
    case kUartInterruptEnablePort:
        io->access_size = 1;
        io->u8 = interrupt_enable_;
        return ZX_OK;
    case kUartLineControlPort:
        io->access_size = 1;
        io->u8 = line_control_;
        return ZX_OK;
    case kUartLineStatusPort:
        io->access_size = 1;
        io->u8 = kUartLineStatusIdle | kUartLineStatusEmpty;
        return ZX_OK;
    case kUartReceivePort:
    case kUartInterruptIdPort:
    case kUartModemControlPort:
    case kUartModemStatusPort... kUartScrScratchPort:
        io->access_size = 1;
        io->u8 = 0;
        return ZX_OK;
    default:
        return ZX_ERR_IO;
    }
}

zx_status_t Uart::Write(uint64_t addr, const IoValue& io) {
    switch (addr) {
    case kUartTransmitPort:
        for (int i = 0; i < io.access_size; i++) {
            tx_buffer_[tx_offset_++] = io.data[i];
            if (tx_offset_ == kUartBufferSize || io.data[i] == '\r') {
                fprintf(stderr, "%.*s", tx_offset_, tx_buffer_);
                fflush(stderr);
                tx_offset_ = 0;
            }
        }
        return ZX_OK;
    case kUartInterruptEnablePort:
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        interrupt_enable_ = io.u8;
        return ZX_OK;
    case kUartLineControlPort:
        if (io.access_size != 1)
            return ZX_ERR_IO_DATA_INTEGRITY;
        line_control_ = io.u8;
        return ZX_OK;
    case kUartInterruptIdPort:
    case kUartModemControlPort... kUartScrScratchPort:
        return ZX_OK;
    default:
        return ZX_ERR_IO;
    }
}
