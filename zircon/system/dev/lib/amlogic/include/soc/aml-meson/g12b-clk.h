// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <soc/aml-meson/aml-clk-common.h>
#include <soc/aml-s905d2/s905d2-hiu.h>

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

// kMesonPllClocks
constexpr uint32_t CLK_GP0_PLL  = AmlClkId(GP0_PLL,  aml_clk_common::aml_clk_type::kMesonPll);
constexpr uint32_t CLK_PCIE_PLL = AmlClkId(PCIE_PLL, aml_clk_common::aml_clk_type::kMesonPll);
constexpr uint32_t CLK_HIFI_PLL = AmlClkId(HIFI_PLL, aml_clk_common::aml_clk_type::kMesonPll);
constexpr uint32_t CLK_SYS_PLL  = AmlClkId(SYS_PLL,  aml_clk_common::aml_clk_type::kMesonPll);
constexpr uint32_t CLK_SYS1_PLL = AmlClkId(SYS1_PLL, aml_clk_common::aml_clk_type::kMesonPll);

}  // namespace g12b_clk
