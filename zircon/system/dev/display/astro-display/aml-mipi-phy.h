// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_AML_MIPI_PHY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_AML_MIPI_PHY_H_

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/dsiimpl.h>
#include <lib/device-protocol/platform-device.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include "aml-dsi.h"
#include "common.h"

namespace astro_display {

class AmlMipiPhy {
 public:
  AmlMipiPhy() {}
  // This function initializes internal state of the object
  zx_status_t Init(zx_device_t* pdev_dev, zx_device_t* dsi_dev, uint32_t lane_num);
  // This function enables and starts up the Mipi Phy
  zx_status_t Startup();
  // This function stops Mipi Phy
  void Shutdown();
  zx_status_t PhyCfgLoad(uint32_t bitrate);
  void Dump();
  uint32_t GetLowPowerEscaseTime() { return dsi_phy_cfg_.lp_tesc; }

 private:
  // This structure holds the timing parameters used for MIPI D-PHY
  // This can be moved later on to MIPI D-PHY specific header if need be
  struct DsiPhyConfig {
    uint32_t lp_tesc;
    uint32_t lp_lpx;
    uint32_t lp_ta_sure;
    uint32_t lp_ta_go;
    uint32_t lp_ta_get;
    uint32_t hs_exit;
    uint32_t hs_trail;
    uint32_t hs_zero;
    uint32_t hs_prepare;
    uint32_t clk_trail;
    uint32_t clk_post;
    uint32_t clk_zero;
    uint32_t clk_prepare;
    uint32_t clk_pre;
    uint32_t init;
    uint32_t wakeup;
  };

  void PhyInit();

  std::optional<ddk::MmioBuffer> dsi_phy_mmio_;
  pdev_protocol_t pdev_ = {nullptr, nullptr};
  uint32_t num_of_lanes_;
  DsiPhyConfig dsi_phy_cfg_;
  ddk::DsiImplProtocolClient dsiimpl_;

  bool initialized_ = false;
  bool phy_enabled_ = false;
};

}  // namespace astro_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_AML_MIPI_PHY_H_
