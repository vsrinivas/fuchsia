// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_PLL_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_PLL_H_

#include <stdint.h>

typedef struct {
  uint64_t rate;
  uint32_t n;
  uint32_t m;
  uint32_t frac;
  uint32_t od;
} hhi_pll_rate_t;

struct reg_sequence {
  uint32_t reg_offset;
  uint32_t def;
  uint32_t delay_us;
};

typedef struct {
  const struct reg_sequence* init_regs;
  uint32_t init_count;
  bool repeatedly_toggling;
} meson_clk_pll_data_t;

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_PLL_H_
