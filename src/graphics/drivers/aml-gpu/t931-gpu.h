// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_AML_GPU_T931_GPU_H_
#define SRC_GRAPHICS_DRIVERS_AML_GPU_T931_GPU_H_

#include <soc/aml-t931/t931-hw.h>

#include "aml-gpu.h"

enum {
  T931_XTAL = 0,  // 24MHz
  T931_GP0 = 1,   // Not used currently.
  T931_HIFI = 2,
  T931_FCLK_DIV2P5 = 3,  // 800 MHz
  T931_FCLK_DIV3 = 4,    // 666 MHz
  T931_FCLK_DIV4 = 5,    // 500 MHz
  T931_FCLK_DIV5 = 6,    // 400 MHz
  T931_FCLK_DIV7 = 7,    // 285.7 MHz
};

static aml_gpu_block_t t931_gpu_blocks = {
    .reset0_level_offset = T931_RESET0_LEVEL,
    .reset0_mask_offset = T931_RESET0_MASK,
    .reset2_level_offset = T931_RESET2_LEVEL,
    .reset2_mask_offset = T931_RESET2_MASK,
    .hhi_clock_cntl_offset = 0x6C,
    .gpu_clk_freq =
        {
            T931_FCLK_DIV7,
            T931_FCLK_DIV5,
            T931_FCLK_DIV4,
            T931_FCLK_DIV3,
            T931_FCLK_DIV2P5,
        },
};

#endif  // SRC_GRAPHICS_DRIVERS_AML_GPU_T931_GPU_H_
