// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-dsi-host.h"

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>

namespace amlogic_display {

#define READ32_MIPI_DSI_REG(a) mipi_dsi_mmio_->Read32(a)
#define WRITE32_MIPI_DSI_REG(a, v) mipi_dsi_mmio_->Write32(v, a)

#define READ32_HHI_REG(a) hhi_mmio_->Read32(a)
#define WRITE32_HHI_REG(a, v) hhi_mmio_->Write32(v, a)

zx_status_t AmlDsiHost::HostModeInit(const display_setting_t& disp_setting) {
  // Setup relevant TOP_CNTL register -- Undocumented --
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_DPI_FORMAT, TOP_CNTL_DPI_CLR_MODE_START,
            TOP_CNTL_DPI_CLR_MODE_BITS);
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, SUPPORTED_VENC_DATA_WIDTH, TOP_CNTL_IN_CLR_MODE_START,
            TOP_CNTL_IN_CLR_MODE_BITS);
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0, TOP_CNTL_CHROMA_SUBSAMPLE_START,
            TOP_CNTL_CHROMA_SUBSAMPLE_BITS);

  // setup dsi config
  dsi_config_t dsi_cfg;
  dsi_cfg.display_setting = disp_setting;
  dsi_cfg.video_mode_type = VIDEO_MODE_BURST;
  dsi_cfg.color_coding = COLOR_CODE_PACKED_24BIT_888;

  designware_config_t dw_cfg;
  dw_cfg.lp_escape_time = phy_->GetLowPowerEscaseTime();
  dw_cfg.lp_cmd_pkt_size = LPCMD_PKT_SIZE;
  dw_cfg.phy_timer_clkhs_to_lp = PHY_TMR_LPCLK_CLKHS_TO_LP;
  dw_cfg.phy_timer_clklp_to_hs = PHY_TMR_LPCLK_CLKLP_TO_HS;
  dw_cfg.phy_timer_hs_to_lp = PHY_TMR_HS_TO_LP;
  dw_cfg.phy_timer_lp_to_hs = PHY_TMR_LP_TO_HS;
  dw_cfg.auto_clklane = 1;
  dsi_cfg.vendor_config_buffer = &dw_cfg;

  dsiimpl_.Config(&dsi_cfg);

  return ZX_OK;
}

void AmlDsiHost::PhyEnable() {
  WRITE32_REG(HHI, HHI_MIPI_CNTL0,
              MIPI_CNTL0_CMN_REF_GEN_CTRL(0x29) | MIPI_CNTL0_VREF_SEL(VREF_SEL_VR) |
                  MIPI_CNTL0_LREF_SEL(LREF_SEL_L_ROUT) | MIPI_CNTL0_LBG_EN |
                  MIPI_CNTL0_VR_TRIM_CNTL(0x7) | MIPI_CNTL0_VR_GEN_FROM_LGB_EN);
  WRITE32_REG(HHI, HHI_MIPI_CNTL1, MIPI_CNTL1_DSI_VBG_EN | MIPI_CNTL1_CTL);
  WRITE32_REG(HHI, HHI_MIPI_CNTL2, MIPI_CNTL2_DEFAULT_VAL);  // 4 lane
}

void AmlDsiHost::PhyDisable() {
  WRITE32_REG(HHI, HHI_MIPI_CNTL0, 0);
  WRITE32_REG(HHI, HHI_MIPI_CNTL1, 0);
  WRITE32_REG(HHI, HHI_MIPI_CNTL2, 0);
}

void AmlDsiHost::HostOff(const display_setting_t& disp_setting) {
  ZX_DEBUG_ASSERT(initialized_);
  // turn host off only if it's been fully turned on
  if (!host_on_) {
    return;
  }

  // Place dsi in command mode first
  dsiimpl_.SetMode(DSI_MODE_COMMAND);

  // Turn off LCD
  lcd_->Disable();

  // disable PHY
  PhyDisable();

  // finally shutdown host
  phy_->Shutdown();

  host_on_ = false;
}

zx_status_t AmlDsiHost::HostOn(const display_setting_t& disp_setting) {
  ZX_DEBUG_ASSERT(initialized_);

  if (host_on_) {
    return ZX_OK;
  }

  // Enable MIPI PHY
  PhyEnable();

  // Create MIPI PHY object
  fbl::AllocChecker ac;
  phy_ = fbl::make_unique_checked<amlogic_display::AmlMipiPhy>(&ac);
  if (!ac.check()) {
    DISP_ERROR("Could not create AmlMipiPhy object\n");
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = phy_->Init(pdev_dev_, dsi_dev_, disp_setting.lane_num);
  if (status != ZX_OK) {
    DISP_ERROR("MIPI PHY Init failed!\n");
    return status;
  }

  // Load Phy configuration
  status = phy_->PhyCfgLoad(bitrate_);
  if (status != ZX_OK) {
    DISP_ERROR("Error during phy config calculations! %d\n", status);
    return status;
  }

  // Enable dwc mipi_dsi_host's clock
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CNTL, 0x3, 4, 2);
  // mipi_dsi_host's reset
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0xf, 0, 4);
  // Release mipi_dsi_host's reset
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_SW_RESET, 0x0, 0, 4);
  // Enable dwc mipi_dsi_host's clock
  SET_BIT32(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL, 0x3, 0, 2);

  WRITE32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD, 0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  // Initialize host in command mode first
  dsiimpl_.SetMode(DSI_MODE_COMMAND);
  if ((status = HostModeInit(disp_setting)) != ZX_OK) {
    DISP_ERROR("Error during dsi host init! %d\n", status);
    return status;
  }

  // Initialize mipi dsi D-phy
  if ((status = phy_->Startup()) != ZX_OK) {
    DISP_ERROR("Error during MIPI D-PHY Initialization! %d\n", status);
    return status;
  }

  // Load LCD Init values while in command mode
  lcd_ = fbl::make_unique_checked<amlogic_display::Lcd>(&ac, panel_type_);
  if (!ac.check()) {
    DISP_ERROR("Failed to create LCD object\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = lcd_->Init(dsi_dev_, lcd_gpio_dev_);
  if (status != ZX_OK) {
    DISP_ERROR("Error during LCD Initialization! %d\n", status);
    return status;
  }

  status = lcd_->Enable();
  if (status != ZX_OK) {
    DISP_ERROR("Could not enable LCD! %d\n", status);
    return status;
  }

  // switch to video mode
  dsiimpl_.SetMode(DSI_MODE_VIDEO);

  // Host is On and Active at this point
  host_on_ = true;
  return ZX_OK;
}

zx_status_t AmlDsiHost::Init() {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(pdev_dev_, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DISP_ERROR("AmlDsiHost: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return status;
  }

  dsiimpl_ = dsi_dev_;

  // Map MIPI DSI and HHI registers
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_MPI_DSI, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map MIPI DSI mmio\n");
    return status;
  }
  mipi_dsi_mmio_ = ddk::MmioBuffer(mmio);

  status = pdev_map_mmio_buffer(&pdev_, MMIO_HHI, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HHI mmio\n");
    return status;
  }
  hhi_mmio_ = ddk::MmioBuffer(mmio);

  initialized_ = true;
  return ZX_OK;
}

void AmlDsiHost::Dump() {
  ZX_DEBUG_ASSERT(initialized_);
  DISP_INFO("MIPI_DSI_TOP_SW_RESET = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SW_RESET));
  DISP_INFO("MIPI_DSI_TOP_CLK_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CLK_CNTL));
  DISP_INFO("MIPI_DSI_TOP_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_CNTL));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_CNTL));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_LINE = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_LINE));
  DISP_INFO("MIPI_DSI_TOP_SUSPEND_PIX = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_SUSPEND_PIX));
  DISP_INFO("MIPI_DSI_TOP_MEAS_CNTL = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_CNTL));
  DISP_INFO("MIPI_DSI_TOP_STAT = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_STAT));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE0 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE0));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_TE1 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_TE1));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS0 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS0));
  DISP_INFO("MIPI_DSI_TOP_MEAS_STAT_VS1 = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEAS_STAT_VS1));
  DISP_INFO("MIPI_DSI_TOP_INTR_CNTL_STAT = 0x%x\n",
            READ32_REG(MIPI_DSI, MIPI_DSI_TOP_INTR_CNTL_STAT));
  DISP_INFO("MIPI_DSI_TOP_MEM_PD = 0x%x\n", READ32_REG(MIPI_DSI, MIPI_DSI_TOP_MEM_PD));
}

}  // namespace amlogic_display
