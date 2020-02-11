// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <soc/aml-common/aml-gpu.h>
#include <soc/aml-s905d2/s905d2-hw.h>

enum {
  S905D2_XTAL = 0,  // 24MHz
  S905D2_GP0 = 1,   // Currently set to 846 MHz
  S905D2_HIFI = 2,
  S905D2_FCLK_DIV2P5 = 3,  // 800 MHz
  S905D2_FCLK_DIV3 = 4,    // 666 MHz
  S905D2_FCLK_DIV4 = 5,    // 500 MHz
  S905D2_FCLK_DIV5 = 6,    // 400 MHz
  S905D2_FCLK_DIV7 = 7,    // 285.7 MHz
};

static aml_gpu_block_t s905d2_gpu_blocks = {
    .reset0_level_offset = S905D2_RESET0_LEVEL,
    .reset0_mask_offset = S905D2_RESET0_MASK,
    .reset2_level_offset = S905D2_RESET2_LEVEL,
    .reset2_mask_offset = S905D2_RESET2_MASK,
    .hhi_clock_cntl_offset = 0x6C,
    .gpu_clk_freq =
        {
            S905D2_FCLK_DIV7,
            S905D2_FCLK_DIV5,
            S905D2_FCLK_DIV4,
            S905D2_FCLK_DIV3,
            S905D2_FCLK_DIV2P5,
            S905D2_GP0,
        },
};
