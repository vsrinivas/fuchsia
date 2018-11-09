// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/mmio.h>
#include <mock_mmio_reg/mock_mmio_reg.h>

// These override the weak methods in mtk-sdmmc-reg.h.

template <>
template <>
uint8_t ddk::MmioBuffer::Read<uint8_t>(zx_off_t offs) const {
    ddk_mock::MockMmioRegRegion* mock_regs =
        reinterpret_cast<ddk_mock::MockMmioRegRegion*>(mmio_.vaddr);
    ZX_ASSERT(mock_regs != nullptr);
    return static_cast<uint8_t>((*mock_regs)[offs].Read());
}

template <>
template <>
uint32_t ddk::MmioBuffer::Read<uint32_t>(zx_off_t offs) const {
    ddk_mock::MockMmioRegRegion* mock_regs =
        reinterpret_cast<ddk_mock::MockMmioRegRegion*>(mmio_.vaddr);
    ZX_ASSERT(mock_regs != nullptr);
    return static_cast<uint32_t>((*mock_regs)[offs].Read());
}

template <>
template <>
void ddk::MmioBuffer::Write<uint32_t>(uint32_t val, zx_off_t offs) const {
    ddk_mock::MockMmioRegRegion* mock_regs =
        reinterpret_cast<ddk_mock::MockMmioRegRegion*>(mmio_.vaddr);
    ZX_ASSERT(mock_regs != nullptr);
    (*mock_regs)[offs].Write(val);
}
