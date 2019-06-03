// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

namespace camera {

// HHI_APICALGDC_CNTL
class GDC_CLK_CNTL : public hwreg::RegisterBase<GDC_CLK_CNTL, uint32_t> {
public:
    DEF_FIELD(27, 25, axi_clk_div);
    DEF_BIT(24, axi_clk_en);
    DEF_FIELD(22, 16, axi_clk_sel);
    DEF_FIELD(11, 9, core_clk_div);
    DEF_BIT(8, core_clk_en);
    DEF_FIELD(6, 0, core_clk_sel);
    static auto Get() {
        return hwreg::RegisterAddr<GDC_CLK_CNTL>(0x16C);
    }
    GDC_CLK_CNTL& reset_axi() {
        set_axi_clk_div(0);
        set_axi_clk_en(0);
        set_axi_clk_sel(0);
        return *this;
    }
    GDC_CLK_CNTL& reset_core() {
        set_core_clk_div(0);
        set_core_clk_en(0);
        set_core_clk_sel(0);
        return *this;
    }
};

// HHI_MEM_PD_REG0
class GDC_MEM_POWER_DOMAIN : public hwreg::RegisterBase<GDC_MEM_POWER_DOMAIN, uint32_t> {
public:
    DEF_FIELD(19, 18, gdc_pd);
    static auto Get() {
        return hwreg::RegisterAddr<GDC_MEM_POWER_DOMAIN>(0x100);
    }
};
} // namespace camera
