// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace board_mt8167 {

struct MtkSdmmcConfig {
  uint32_t fifo_depth;
  uint32_t src_clk_freq;
  bool is_sdio;
};

}  // namespace board_mt8167
