// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_CLK_COMMON_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_CLK_COMMON_H_

namespace aml_clk_common {

enum class aml_clk_type : uint16_t {
  kMesonGate = (1 << 0),
  kMesonPll = (1 << 1),
  kMesonMux = (1 << 2),
  kMesonMuxRo = (1 << 3),
  kMesonCpuClk = (1 << 4),
};

// Create a clock ID based on a type and an index
constexpr uint32_t AmlClkId(const uint16_t index, const aml_clk_type type) {
  // Top 16 bits are the type, bottom 16 bits are the index.
  return static_cast<uint32_t>(index) | ((static_cast<uint32_t>(type)) << 16);
}

constexpr uint16_t AmlClkIndex(const uint32_t clk_id) { return clk_id & 0x0000ffff; }

constexpr aml_clk_type AmlClkType(const uint32_t clk_id) {
  return static_cast<aml_clk_type>(clk_id >> 16);
}

}  // namespace aml_clk_common

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_CLK_COMMON_H_
