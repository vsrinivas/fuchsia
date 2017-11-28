// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mmio-space.h"

namespace i915 {

MmioSpace::MmioSpace(uintptr_t base) : base_(base) {}

uint32_t MmioSpace::Read32(uint32_t offset) {
    return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
}

void MmioSpace::Write32(uint32_t offset, uint32_t val) {
    *reinterpret_cast<volatile uint32_t*>(base_ + offset) = val;
}

void MmioSpace::Write64(uint32_t offset, uint64_t val) {
    *reinterpret_cast<volatile uint64_t*>(base_ + offset) = val;
}

} // namespace i915
