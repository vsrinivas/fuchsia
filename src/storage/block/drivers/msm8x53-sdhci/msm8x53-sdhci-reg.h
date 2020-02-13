// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_REG_H_
#define SRC_STORAGE_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_REG_H_

#include <hwreg/bitfields.h>

namespace sdhci {

class CoreHcMode : public hwreg::RegisterBase<CoreHcMode, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<CoreHcMode>(0x078); }

  DEF_BIT(13, ff_clk_sw_rst_disable);
  DEF_BIT(0, hc_mode_en);
};

class HcVendorSpec3 : public hwreg::RegisterBase<HcVendorSpec3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<HcVendorSpec3>(0x1b0); }

  DEF_BIT(10, alt_fifo_en);
};

}  // namespace sdhci

#endif  // SRC_STORAGE_BLOCK_DRIVERS_MSM8X53_SDHCI_MSM8X53_SDHCI_REG_H_
