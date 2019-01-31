// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/mmio.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

// These override the weak methods in mt8167-gpio-regs.h.
// mock-mmio uses vaddr as a key to find MockMmioRegRegion, need to substract offset.
template <>
template <>
uint16_t ddk::MmioBuffer::Read<uint16_t>(zx_off_t offs) const {
    ddk_mock::MockMmioRegRegion* mock_regs =
        reinterpret_cast<ddk_mock::MockMmioRegRegion*>(
            reinterpret_cast<uintptr_t>(mmio_.vaddr) - mmio_.offset);
    ZX_ASSERT(mock_regs != nullptr);
    return static_cast<uint16_t>((*mock_regs)[mmio_.offset + offs].Read());
}

template <>
template <>
void ddk::MmioBuffer::Write<uint16_t>(uint16_t val, zx_off_t offs) const {
    ddk_mock::MockMmioRegRegion* mock_regs =
        reinterpret_cast<ddk_mock::MockMmioRegRegion*>(
            reinterpret_cast<uintptr_t>(mmio_.vaddr) - mmio_.offset);
    ZX_ASSERT(mock_regs != nullptr);
    (*mock_regs)[mmio_.offset + offs].Write(val);
}
