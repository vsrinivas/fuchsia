// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

typedef enum g12b_clk_gate_idx {
  // SYS CPU CLK
  G12B_CLK_SYS_PLL_DIV16 = 0,
  G12B_CLK_SYS_CPU_CLK_DIV16 = 1,

  // GPIO 24MHz
  G12B_CLK_CAM_INCK_24M = 2,

  // NB: This must be the last entry
  CLK_G12B_COUNT,
} g12b_clk_gate_idx_t;
