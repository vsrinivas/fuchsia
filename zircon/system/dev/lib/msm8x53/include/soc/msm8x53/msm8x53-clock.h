// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace msm8x53 {

// Branch clock control register.
class CBCR : public hwreg::RegisterBase<CBCR, uint32_t> {
    DEF_BIT(0, enable);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<CBCR>(offset); }
};

// Branch clock reset register.
class BCR : public hwreg::RegisterBase<BCR, uint32_t> {
    DEF_BIT(0, reset);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<BCR>(offset); }
};

// Root clock gating command register.
class RCG_CMD : public hwreg::RegisterBase<RCG_CMD, uint32_t> {
    DEF_BIT(0, update);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_CMD>(offset); }
};

// Root clock gating config register.
class RCG_CFG: public hwreg::RegisterBae<RCG_CFG, uint32_t> {
    DEF_FIELD(12, 11, mode);
    DEF_FIELD(8, 6, src_sel);
    DEF_FIELD(4, 0, src_div);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_CFG>(offset); }
};

// Root clock gating M-prescalar.
class RCG_M : public hwreg::RegisterBase<RCG_M, uint32_t> {
    DEF_FIELD(31, 0, m);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_M>(offset); }
};

// Root clock gating N-prescalar.
class RCG_N : public hwreg::RegisterBase<RCG_N, uint32_t> {
    DEF_FIELD(31, 0, n);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_N>(offset); }
};

// Root clock gating D-prescalar.
class RCG_D : public hwreg::RegisterBase<RCG_D, uint32_t> {
    DEF_FIELD(31, 0, d);
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<RCG_D>(offset); }
};

} // namespace msm8x53
