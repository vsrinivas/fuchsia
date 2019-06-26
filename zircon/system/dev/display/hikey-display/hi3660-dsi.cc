// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hi3660-dsi.h"

#include <hw/reg.h>
#include <unistd.h>

#include "hidisplay-regs.h"

namespace hi_display {

zx_status_t HiDsi::GetDisplayResolution(uint32_t& width, uint32_t& height) {
  if (std_disp_timing_ == NULL) {
    zxlogf(ERROR, "Display not ready \n");
    return ZX_ERR_NOT_SUPPORTED;
  }
  height = static_cast<uint32_t>(std_disp_timing_->HActive);
  width = static_cast<uint32_t>(std_disp_timing_->VActive);

  return ZX_OK;
}

zx_status_t HiDsi::DsiGetDisplayTiming() {
  zx_status_t status;
  uint8_t num_dtd = 0;

  if (&edid_buf_[0] == 0) {
    zxlogf(ERROR, "%s: No EDID available\n", __FUNCTION__);
    return ZX_ERR_NOT_FOUND;
  }

  std_raw_dtd_ = static_cast<DetailedTiming*>(calloc(1, sizeof(DetailedTiming)));
  std_disp_timing_ = static_cast<DisplayTiming*>(calloc(1, sizeof(DisplayTiming)));
  if (std_raw_dtd_ == 0 || std_disp_timing_ == 0) {
    return ZX_ERR_NO_MEMORY;
  }

  edid_->EdidParseStdDisplayTiming(edid_buf_, std_raw_dtd_, std_disp_timing_);

  if ((status = edid_->EdidGetNumDtd(edid_buf_, &num_dtd)) != ZX_OK) {
    zxlogf(ERROR, "Something went wrong with reading number of DTD\n");
    return status;
  }

  if (num_dtd == 0) {
    zxlogf(ERROR, "No DTD Founds!!\n");
    return ZX_ERR_INTERNAL;
  }

  zxlogf(INFO, "Number of DTD found was %d\n", num_dtd);
  raw_dtd_ = static_cast<DetailedTiming*>(calloc(num_dtd, sizeof(DetailedTiming)));
  disp_timing_ = static_cast<DisplayTiming*>(calloc(num_dtd, sizeof(DisplayTiming)));
  if (raw_dtd_ == 0 || disp_timing_ == 0) {
    return ZX_ERR_NO_MEMORY;
  }

  edid_->EdidParseDisplayTiming(edid_buf_, raw_dtd_, disp_timing_, num_dtd);

  return ZX_OK;
}

/* Unknown DPHY.  Use hardcoded values from Android source code for now */
void HiDsi::DsiDphyWrite(uint32_t reg, uint32_t val) {
  dsiimpl_.PhySendCode((reg | DW_DSI_PHY_TST_CTRL1_TESTEN), val);
}

void HiDsi::DsiConfigureDphyPll() {
  uint32_t i;
  uint32_t tmp = 0;

  // TODO: Calculate proper configuration based on display resolution
  // Currently hardcoded configuration to support only 1080p
  if (std_disp_timing_->HActive == 1920 && std_disp_timing_->VActive == 1080) {
    DsiDphyWrite(0x15, 0x0d);
    DsiDphyWrite(0x16, 0x21);
    DsiDphyWrite(0x1e, 0x29);
    DsiDphyWrite(0x1f, 0x5a);

    DsiDphyWrite(0x21, 0x30);
    DsiDphyWrite(0x22, 0x15);
    DsiDphyWrite(0x23, 0x04);
    DsiDphyWrite(0x24, 0x1c);

    for (i = 0; i < DS_NUM_LANES; i++) {
      tmp = i << 4;
      DsiDphyWrite(0x30 + tmp, 0x55);
      DsiDphyWrite(0x32 + tmp, 0x15);
      DsiDphyWrite(0x33 + tmp, 0x04);
      DsiDphyWrite(0x34 + tmp, 0x1c);
    }
  } else {
    zxlogf(INFO, "%d x %d resolution not supported\n", std_disp_timing_->HActive,
           std_disp_timing_->VActive);
  }
}

zx_status_t HiDsi::DsiConfigureDphy() {
  // Configure PHY PLL values
  DsiConfigureDphyPll();

  // Enable PHY
  dsiimpl_.PhyPowerUp();

  // Wait for PHY to be read
  zx_status_t status;
  if ((status = dsiimpl_.PhyWaitForReady()) != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

zx_status_t HiDsi::DsiHostConfig(const display_setting_t& disp_setting) {
  constexpr uint32_t kClkLaneLp2Hs = 0x3f;
  constexpr uint32_t kClkLaneHs2Lp = 0x3a;
  constexpr uint32_t kDataLaneLp2Hs = 0x68;
  constexpr uint32_t kDataLaneHs2Lp = 0x13;

  dsi_config_t dsi_cfg;
  dsi_cfg.display_setting = disp_setting;
  dsi_cfg.video_mode_type = VIDEO_MODE_NON_BURST_PULSE;
  dsi_cfg.color_coding = COLOR_CODE_PACKED_24BIT_888;

  designware_config_t dw_cfg;
  dw_cfg.lp_escape_time = 0x9;
  dw_cfg.lp_cmd_pkt_size = 4;
  dw_cfg.phy_timer_clkhs_to_lp = kClkLaneHs2Lp;
  dw_cfg.phy_timer_clklp_to_hs = kClkLaneLp2Hs;
  dw_cfg.phy_timer_hs_to_lp = kDataLaneHs2Lp;
  dw_cfg.phy_timer_lp_to_hs = kDataLaneLp2Hs;
  dw_cfg.auto_clklane = 0;
  dsi_cfg.vendor_config_buffer = &dw_cfg;
  dsiimpl_.Config(&dsi_cfg);

  return ZX_OK;
}

zx_status_t HiDsi::HiDsiGetDisplaySetting(display_setting_t& disp_setting) {
  uint32_t hsync_start = std_disp_timing_->HActive + std_disp_timing_->HSyncOffset;
  uint32_t vsync_start = std_disp_timing_->VActive + std_disp_timing_->VSyncOffset;
  uint32_t hsync_end = hsync_start + std_disp_timing_->HSyncPulseWidth;
  uint32_t vsync_end = vsync_start + std_disp_timing_->VSyncPulseWidth;
  uint32_t htotal = std_disp_timing_->HActive + std_disp_timing_->HBlanking;
  uint32_t vtotal = std_disp_timing_->VActive + std_disp_timing_->VBlanking;

  disp_setting.h_active = std_disp_timing_->HActive;
  disp_setting.v_active = std_disp_timing_->VActive;
  disp_setting.vsync_bp = vtotal - vsync_end;
  disp_setting.hsync_bp = htotal - hsync_end;
  disp_setting.h_period = htotal;
  disp_setting.v_period = vtotal;
  disp_setting.hsync_width = std_disp_timing_->HSyncPulseWidth;
  disp_setting.vsync_width = std_disp_timing_->VSyncPulseWidth;

  return ZX_OK;
}

zx_status_t HiDsi::DsiMipiInit() {
  dsiimpl_.PowerDown();

  DsiConfigureDphy();

  // Configure the dsi settings and initialize dsiimpl
  display_setting_t disp_setting;
  disp_setting.lane_num = DS_NUM_LANES;
  HiDsiGetDisplaySetting(disp_setting);
  DsiHostConfig(disp_setting);

  /* Waking up Core*/
  dsiimpl_.PowerUp();

  /* Make sure we are in video mode  */
  dsiimpl_.SetMode(DSI_MODE_VIDEO);

  return ZX_OK;
}

zx_status_t HiDsi::DsiInit(zx_device_t* parent) {
  pdev_protocol_t pdev;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to obtain the display protocol\n");
    return ZX_ERR_NO_MEMORY;
  }

  dsiimpl_ = parent;
  if (!dsiimpl_.is_valid()) {
    zxlogf(ERROR, "DSI Protocol Not implemented\n");
    return ZX_ERR_NO_RESOURCES;
  }

  DsiGetDisplayTiming();
  DsiMipiInit();
#ifdef DW_DSI_TEST_ENABLE
  dsiimpl_.PrintDsiRegisters();
  while (true)
    dsiimpl_.EnableBist(0);
#endif

  return ZX_OK;
}
}  // namespace hi_display
