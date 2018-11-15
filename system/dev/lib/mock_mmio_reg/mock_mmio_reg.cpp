// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock_mmio_reg/mock_mmio_reg.h"

void mmio_fake_read(uintptr_t base, size_t size, zx_off_t off, void* value) {
    ZX_ASSERT(size == sizeof(uint8_t) ||
              size == sizeof(uint16_t) ||
              size == sizeof(uint32_t) ||
              size == sizeof(uint64_t));

    ddk_mock::MockMmioRegRegion* mock_regs = reinterpret_cast<ddk_mock::MockMmioRegRegion*>(base);
    ZX_ASSERT(mock_regs != nullptr);

    uint64_t value_64 = (*mock_regs)[off].Read();

    switch (size) {
    case sizeof(uint8_t):
        *reinterpret_cast<uint8_t*>(value) = static_cast<uint8_t>(value_64);
        break;
    case sizeof(uint16_t):
        *reinterpret_cast<uint16_t*>(value) = static_cast<uint16_t>(value_64);
        break;
    case sizeof(uint32_t):
        *reinterpret_cast<uint32_t*>(value) = static_cast<uint32_t>(value_64);
        break;
    case sizeof(uint64_t):
        *reinterpret_cast<uint64_t*>(value) = value_64;
        break;
    }
}

void mmio_fake_write(uintptr_t base, size_t size, const void* value, zx_off_t off) {
    ZX_ASSERT(size == sizeof(uint8_t) ||
              size == sizeof(uint16_t) ||
              size == sizeof(uint32_t) ||
              size == sizeof(uint64_t));

    ddk_mock::MockMmioRegRegion* mock_regs = reinterpret_cast<ddk_mock::MockMmioRegRegion*>(base);
    ZX_ASSERT(mock_regs != nullptr);

    switch (size) {
    case sizeof(uint8_t):
        (*mock_regs)[off].Write(*reinterpret_cast<const uint8_t*>(value));
        break;
    case sizeof(uint16_t):
        (*mock_regs)[off].Write(*reinterpret_cast<const uint16_t*>(value));
        break;
    case sizeof(uint32_t):
        (*mock_regs)[off].Write(*reinterpret_cast<const uint32_t*>(value));
        break;
    case sizeof(uint64_t):
        (*mock_regs)[off].Write(*reinterpret_cast<const uint64_t*>(value));
        break;
    }
}
