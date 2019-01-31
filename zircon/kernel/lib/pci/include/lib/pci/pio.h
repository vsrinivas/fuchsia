// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/types.h>

namespace Pci {
    // Returns the BDF address without the bottom two bits masked off.
    constexpr uint32_t PciBdfRawAddr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
        return (((bus & 0xFF) << 16) | // bits 23-16 bus
                ((dev & 0x1F) << 11) | // bits 15-11 device
                ((func & 0x7) << 8) |  // bifs 10-8 func
                (off & 0xFF));         // bits 7-2 register, WITH bottom two bits as well
    }

    // Return BDF address with bottom two bits masked off.
    constexpr uint32_t PciBdfAddr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
        // Bits 1 & 0 must be zero
        return PciBdfRawAddr(bus, dev, func, off) & ~0x3;
    }

    // |addr| is expected to NOT have the bottom to bits masked off
    zx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width);
    zx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
            uint32_t* val, size_t width);

    // |addr| is expected to NOT have the bottom to bits masked off
    zx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width);
    zx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
            uint32_t val, size_t width);
}
