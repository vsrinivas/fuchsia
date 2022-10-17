// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_MIPI_PHY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_MIPI_PHY_H_

#include <fuchsia/hardware/dsiimpl/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <optional>

#include <ddktl/device.h>

#include "common.h"
#include "dsi.h"

namespace amlogic_display {

class MipiPhy {
 public:
  // This function initializes internal state of the object
  static zx::result<std::unique_ptr<MipiPhy>> Create(ddk::PDev& pdev,
                                                     ddk::DsiImplProtocolClient dsi,
                                                     bool already_enabled);
  // This function enables and starts up the Mipi Phy
  zx_status_t Startup();
  // This function stops Mipi Phy
  void Shutdown();
  zx_status_t PhyCfgLoad(uint32_t bitrate);
  void Dump();
  uint32_t GetLowPowerEscaseTime() { return dsi_phy_cfg_.lp_tesc; }

 private:
  MipiPhy() = default;
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

  std::optional<fdf::MmioBuffer> dsi_phy_mmio_;
  DsiPhyConfig dsi_phy_cfg_;
  ddk::DsiImplProtocolClient dsiimpl_;

  bool phy_enabled_ = false;
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_MIPI_PHY_H_
