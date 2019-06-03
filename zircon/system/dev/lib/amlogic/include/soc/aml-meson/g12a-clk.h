// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

typedef enum g12a_clk_gate_idx {
    // SYS CPU CLK
    CLK_SYS_PLL_DIV16 = 0,
    CLK_SYS_CPU_CLK_DIV16 = 1,

    // NB: This must be the last entry
    CLK_G12A_COUNT,
} g12a_clk_gate_idx_t;
