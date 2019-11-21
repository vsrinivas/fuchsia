// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <soc/aml-meson/aml-clk-common.h>

namespace g12b_clk {

// SYS CPU CLK
constexpr uint32_t G12B_CLK_SYS_PLL_DIV16 = AmlClkId(0, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t G12B_CLK_SYS_CPU_CLK_DIV16 = AmlClkId(1, aml_clk_common::aml_clk_type::kMesonGate);

// GPIO 24MHz
constexpr uint32_t G12B_CLK_CAM_INCK_24M = AmlClkId(2, aml_clk_common::aml_clk_type::kMesonGate);

// SYS CPUB CLK
constexpr uint32_t G12B_CLK_SYS_PLLB_DIV16 = AmlClkId(3, aml_clk_common::aml_clk_type::kMesonGate);
constexpr uint32_t G12B_CLK_SYS_CPUB_CLK_DIV16 = AmlClkId(4, aml_clk_common::aml_clk_type::kMesonGate);

// NB: This must be the last entry
constexpr uint32_t CLK_G12B_COUNT = 5;

}  // namespace g12b_clk
