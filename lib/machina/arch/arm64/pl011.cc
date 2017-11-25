// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <machina/uart.h>

#include <stdio.h>

#include <hypervisor/address.h>
#include <hypervisor/guest.h>

// PL011 registers.
enum class Pl011Register : uint64_t {
    DR = 0x00,
    FR = 0x18,
    CR = 0x30,
};

zx_status_t Pl011::Init(Guest* guest, uint64_t addr) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, addr, PL011_SIZE, 0, this);
}

zx_status_t Pl011::Read(uint64_t addr, IoValue* io) const {
    switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::FR:
        if (io->access_size != 4)
            return ZX_ERR_IO_DATA_INTEGRITY;
        io->u32 = 0;
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled PL011 address %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

zx_status_t Pl011::Write(uint64_t addr, const IoValue& io) {
    switch (static_cast<Pl011Register>(addr)) {
    case Pl011Register::DR:
        Print(io.u8);
        return ZX_OK;
    case Pl011Register::CR:
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled PL011 address %#lx\n", addr);
        return ZX_ERR_IO;
    }
}

void Pl011::Print(uint8_t ch) {
    tx_buffer_[tx_offset_++] = ch;
    if (tx_offset_ < kBufferSize && ch != '\r')
        return;
    fprintf(stdout, "%.*s", tx_offset_, tx_buffer_);
    fflush(stdout);
    tx_offset_ = 0;
}
