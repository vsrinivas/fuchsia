// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/types.h>

namespace Pci {
    constexpr uint32_t PciBdfAddr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
        return (((bus & 0xFF) << 16) | // bits 23-16 bus
                ((dev & 0x1F) << 11) | // bits 15-11 device
                ((func & 0x7) << 8) |  // bifs 10-8 func
                (off & 0xFC));         // bits 7-2 register, bits 1 & 0 must be zero
    }

    mx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width);
    mx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
            uint32_t* val, size_t width);
    mx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width);
    mx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
            uint32_t val, size_t width);
}
