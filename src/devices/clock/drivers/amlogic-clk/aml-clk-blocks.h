// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

typedef struct meson_clk_gate {
  uint32_t reg;  // Offset from Clock Base Addr in bytes.
  uint32_t bit;  // Offset into this register.
} meson_clk_gate_t;

typedef struct meson_clk_msr {
  uint32_t reg0_offset;  // Offset of MSR_CLK_REG0 from MSR_CLK Base Addr
  uint32_t reg2_offset;  // Offset of MSR_CLK_REG2 from MSR_CLK Base Addr
} meson_clk_msr_t;

typedef struct meson_clk_mux {
  uint32_t reg;        // Offset from Clock Base in bytes.
  uint32_t mask;       // Right Justified Mask of the mux selection bits.
  uint32_t shift;      // Offset of the Mux input index in the register in bits.
  uint32_t n_inputs;   // Number of possible inputs to select from.
  const uint32_t *inputs;   // If set, this field maps indicies to mux selection values
                            // since indices must always be in the range [0, n_inputs).
} meson_clk_mux_t;
