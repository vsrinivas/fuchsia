// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "registers-base.h"

namespace registers {

// Graphics & Memory Controller Hub Graphics Control - GGC_0_0_0_PCI
// This is a 16-bit register, so it needs to be populated manually
class GmchGfxControl : public RegisterBase<GmchGfxControl> {
public:
    static constexpr uint32_t kAddr = 0x50;

    DEF_FIELD(7, 6, gtt_gfx_mem_size);

    static inline uint32_t mem_size_to_mb(uint32_t val) {
        return val ? 1 << (20 + val) : 0;
    }
};

// PLANE_SURF, plane 1-A
class PlaneSurface : public RegisterBase<PlaneSurface> {
public:
    static constexpr uint32_t kAddr = 0x7019c;

    DEF_FIELD(31, 12, surface_base_addr);
    static constexpr uint32_t kRShiftCount = 12;
    static constexpr uint32_t kLinearAlignment = 256 * 1024;
    static constexpr uint32_t kXTilingAlignment = 256 * 1024;
    static constexpr uint32_t kYTilingAlignment = 1024 * 1024;
    static constexpr uint32_t kTrailingPtePadding = 136;
    static constexpr uint32_t kHeaderPtePaddingFor180Or270 = 136;

    static RegisterAddr<PlaneSurface> Get() {
        return RegisterAddr<PlaneSurface>(kAddr);
    }
};

// MASTER_INT_CTL
class MasterInterruptControl : public RegisterBase<MasterInterruptControl> {
public:
    DEF_BIT(31, enable_mask);
    DEF_BIT(23, sde_int_pending);

    static RegisterAddr<MasterInterruptControl> Get() {
        return RegisterAddr<MasterInterruptControl>(0x44200);
    }
};

} // namespace registers
