// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/pl011.h"

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/guest.h>

// clang-format off

// PL011 registers.
enum class Pl011Register : uint64_t {
    DR      = 0x00,
    FR      = 0x18,
    CR      = 0x30,
    IFLS    = 0x34,
    IMSC    = 0x38,
    ICR     = 0x44,
};

// clang-format on

zx_status_t Pl011::Init(Guest* guest, uint64_t addr) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, addr, PL011_SIZE, 0, this);
}

zx_status_t Pl011::Read(uint64_t addr, IoValue* io) const {
    switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR:
        {
            fbl::AutoLock lock(&mutex_);
            io->u16 = control_;
        }
        return ZX_OK;
    case Pl011Register::FR:
        io->u16 = 0;
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled PL011 address read %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

zx_status_t Pl011::Write(uint64_t addr, const IoValue& io) {
    switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::CR:
        {
            fbl::AutoLock lock(&mutex_);
            control_ = io.u16;
        }
        return ZX_OK;
    case Pl011Register::DR:
        Print(io.u8);
        return ZX_OK;
    case Pl011Register::ICR:
    case Pl011Register::IFLS:
    case Pl011Register::IMSC:
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled PL011 address write %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

void Pl011::Print(uint8_t ch) {
    {
        fbl::AutoLock lock(&mutex_);
        tx_buffer_[tx_offset_++] = ch;
        if (tx_offset_ < kBufferSize && ch != '\r')
            return;
        fprintf(stdout, "%.*s", tx_offset_, tx_buffer_);
        tx_offset_ = 0;
    }
    fflush(stdout);
}
