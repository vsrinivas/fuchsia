// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_SDMMC_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_SDMMC_H_

namespace board_mt8167 {

struct MtkSdmmcConfig {
  uint32_t fifo_depth;
  uint32_t src_clk_freq;
  bool is_sdio;
};

}  // namespace board_mt8167

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_SDMMC_H_
