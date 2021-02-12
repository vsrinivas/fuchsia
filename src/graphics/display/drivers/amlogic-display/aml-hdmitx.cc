// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-hdmitx.h"

#include "hdmitx-cbus-regs.h"
#include "hdmitx-dwc-regs.h"
#include "hdmitx-hhi-regs.h"
#include "hdmitx-top-regs.h"
#include "hdmitx-vpu-regs.h"

namespace amlogic_display {

namespace {

struct reg_val_pair {
  uint32_t reg;
  uint32_t val;
};

static const struct reg_val_pair ENC_LUT_GEN[] = {
    {VPU_ENCP_VIDEO_EN, 0},           {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_MODE, 0x4040},    {VPU_ENCP_VIDEO_MODE_ADV, 0x18},
    {VPU_VPU_VIU_VENC_MUX_CTRL, 0xA}, {VPU_ENCP_VIDEO_VSO_BEGIN, 16},
    {VPU_ENCP_VIDEO_VSO_END, 32},     {VPU_ENCI_VIDEO_EN, 0},
    {VPU_ENCP_VIDEO_EN, 1},           {0xFFFFFFFF, 0},
};

}  // namespace

void AmlHdmitx::WriteReg(uint32_t addr, uint32_t data) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t offset = (addr & DWC_OFFSET_MASK) >> 24;
  addr = addr & 0xffff;

  fbl::AutoLock lock(&register_lock_);
  if (offset) {
    hdmitx_mmio_->Write8(data & 0xFF, addr);
  } else {
    hdmitx_mmio_->Write32(data, (addr << 2) + 0x8000);
  }

#ifdef LOG_HDMITX
  DISP_INFO("%s wr[0x%x] 0x%x\n", offset ? "DWC" : "TOP", addr, data);
#endif
}

uint32_t AmlHdmitx::ReadReg(uint32_t addr) {
  // determine if we are writing to HDMI TOP (AMLOGIC Wrapper) or HDMI IP
  uint32_t ret = 0;
  uint32_t offset = (addr & DWC_OFFSET_MASK) >> 24;
  addr = addr & 0xffff;

  fbl::AutoLock lock(&register_lock_);
  if (offset) {
    ret = hdmitx_mmio_->Read8(addr);
  } else {
    ret = hdmitx_mmio_->Read32((addr << 2) + 0x8000);
  }

  return ret;
}

void AmlHdmitx::ScdcWrite(uint8_t addr, uint8_t val) {
  WriteReg(HDMITX_DWC_I2CM_SLAVE, 0x54);
  WriteReg(HDMITX_DWC_I2CM_ADDRESS, addr);
  WriteReg(HDMITX_DWC_I2CM_DATAO, val);
  WriteReg(HDMITX_DWC_I2CM_OPERATION, 0x10);
  usleep(2000);
}

void AmlHdmitx::ScdcRead(uint8_t addr, uint8_t* val) {
  WriteReg(HDMITX_DWC_I2CM_SLAVE, 0x54);
  WriteReg(HDMITX_DWC_I2CM_ADDRESS, addr);
  WriteReg(HDMITX_DWC_I2CM_OPERATION, 1);
  usleep(2000);
  *val = (uint8_t)ReadReg(HDMITX_DWC_I2CM_DATAI);
}

zx_status_t AmlHdmitx::Init() {
  if (!pdev_.is_valid()) {
    DISP_ERROR("AmlHdmitx: Could not get ZX_PROTOCOL_PDEV protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Map registers
  zx_status_t status = pdev_.MapMmio(MMIO_VPU, &vpu_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map VPU mmio\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_MPI_DSI, &hdmitx_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HDMITX mmio\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_HHI, &hhi_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map HHI mmio\n");
    return status;
  }

  status = pdev_.MapMmio(MMIO_CBUS, &cbus_mmio_);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map CBUS mmio\n");
    return status;
  }

  return InitHw();
}

zx_status_t AmlHdmitx::InitHw() {
  /* Step 1: Initialize various clocks related to the HDMI Interface*/
  SET_BIT32(CBUS, PAD_PULL_UP_EN_REG3, 0, 0, 2);
  SET_BIT32(CBUS, PAD_PULL_UP_REG3, 0, 0, 2);
  SET_BIT32(CBUS, P_PREG_PAD_GPIO3_EN_N, 3, 0, 2);
  SET_BIT32(CBUS, PERIPHS_PIN_MUX_B, 0x11, 0, 8);

  // enable clocks
  HhiHdmiClkCntlReg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_clk_div(0)
      .set_clk_en(1)
      .set_clk_sel(0)
      .WriteTo(&(*hhi_mmio_));

  // enable clk81 (needed for HDMI module and a bunch of other modules)
  HhiGclkMpeg2Reg::Get().ReadFrom(&(*hhi_mmio_)).set_clk81_en(1).WriteTo(&(*hhi_mmio_));

  // power up HDMI Memory (bits 15:8)
  HhiMemPdReg0::Get().ReadFrom(&(*hhi_mmio_)).set_hdmi(0).WriteTo(&(*hhi_mmio_));

  // TODO(fxb/69679): Add in Resets
  // reset hdmi related blocks (HIU, HDMI SYS, HDMI_TX)
  // auto reset0_result = display->reset_register_.WriteRegister32(PRESET0_REGISTER, 1 << 19, 1 <<
  // 19); if ((reset0_result.status() != ZX_OK) || reset0_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset0 Write failed\n");
  // }

  /* FIXME: This will reset the entire HDMI subsystem including the HDCP engine.
   * At this point, we have no way of initializing HDCP block, so we need to
   * skip this for now.
   */
  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 15, 1 <<
  // 15); // Will mess up hdcp stuff if ((reset2_result.status() != ZX_OK) ||
  // reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // auto reset2_result = display->reset_register_.WriteRegister32(PRESET2_REGISTER, 1 << 2, 1 <<
  // 2); if ((reset2_result.status() != ZX_OK) || reset2_result->result.is_err()) {
  //   zxlogf(ERROR, "Reset2 Write failed\n");
  // }

  // Bring HDMI out of reset
  WriteReg(HDMITX_TOP_SW_RESET, 0);
  usleep(200);
  WriteReg(HDMITX_TOP_CLK_CNTL, 0x000000ff);
  WriteReg(HDMITX_DWC_MC_LOCKONCLOCK, 0xff);
  WriteReg(HDMITX_DWC_MC_CLKDIS, 0x00);

  /* Step 2: Initialize DDC Interface (For EDID) */

  // FIXME: Pinmux i2c pins (skip for now since uboot it doing it)

  // Configure i2c interface
  // a. disable all interrupts (read_req, done, nack, arbitration)
  WriteReg(HDMITX_DWC_I2CM_INT, 0);
  WriteReg(HDMITX_DWC_I2CM_CTLINT, 0);

  // b. set interface to standard mode
  WriteReg(HDMITX_DWC_I2CM_DIV, 0);

  // c. Setup i2c timings (based on u-boot source)
  WriteReg(HDMITX_DWC_I2CM_SS_SCL_HCNT_1, 0);
  WriteReg(HDMITX_DWC_I2CM_SS_SCL_HCNT_0, 0xcf);
  WriteReg(HDMITX_DWC_I2CM_SS_SCL_LCNT_1, 0);
  WriteReg(HDMITX_DWC_I2CM_SS_SCL_LCNT_0, 0xff);
  WriteReg(HDMITX_DWC_I2CM_FS_SCL_HCNT_1, 0);
  WriteReg(HDMITX_DWC_I2CM_FS_SCL_HCNT_0, 0x0f);
  WriteReg(HDMITX_DWC_I2CM_FS_SCL_LCNT_1, 0);
  WriteReg(HDMITX_DWC_I2CM_FS_SCL_LCNT_0, 0x20);
  WriteReg(HDMITX_DWC_I2CM_SDA_HOLD, 0x08);

  // d. disable any SCDC operations for now
  WriteReg(HDMITX_DWC_I2CM_SCDC_UPDATE, 0);
  DISP_INFO("done!!\n");

  return ZX_OK;
}

zx_status_t AmlHdmitx::InitInterface() {
  // FIXME: Need documentation for HDMI PLL initialization
  ConfigurePll(&p_, &p_.pll_p_24b);

  for (size_t i = 0; ENC_LUT_GEN[i].reg != 0xFFFFFFFF; i++) {
    WRITE32_REG(VPU, ENC_LUT_GEN[i].reg, ENC_LUT_GEN[i].val);
  }

  WRITE32_REG(
      VPU, VPU_ENCP_VIDEO_MAX_PXCNT,
      (p_.timings.venc_pixel_repeat) ? ((p_.timings.htotal << 1) - 1) : (p_.timings.htotal - 1));
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_MAX_LNCNT, p_.timings.vtotal - 1);

  if (p_.timings.venc_pixel_repeat) {
    SET_BIT32(VPU, VPU_ENCP_VIDEO_MODE_ADV, 1, 0, 1);
  }

  // Configure Encoder with detailed timing info (based on resolution)
  ConfigEncoder(&p_);

  // Configure VDAC
  WRITE32_REG(HHI, HHI_VDAC_CNTL0_G12A, 0);
  WRITE32_REG(HHI, HHI_VDAC_CNTL1_G12A, 8);  // set Cdac_pwd [whatever that is]

  // Configure HDMI TX IP
  ConfigHdmitx(&p_);

  if (p_.is4K) {
    // Setup TMDS Clocks (magic numbers)
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x03ff03ff);
    WriteReg(HDMITX_DWC_FC_SCRAMBLER_CTRL, ReadReg(HDMITX_DWC_FC_SCRAMBLER_CTRL) | (1 << 0));
  } else {
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_01, 0x001f001f);
    WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_23, 0x001f001f);
    WriteReg(HDMITX_DWC_FC_SCRAMBLER_CTRL, 0);
  }

  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
  usleep(2);
  WriteReg(HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

  uint8_t scdc_data = 0;
  ScdcRead(0x1, &scdc_data);
  DISP_INFO("version is %s\n", (scdc_data == 1) ? "2.0" : "<= 1.4");
  // scdc write is done twice in uboot
  // TODO: find scdc register def
  ScdcWrite(0x2, 0x1);
  ScdcWrite(0x2, 0x1);

  if (p_.is4K) {
    ScdcWrite(0x20, 3);
    ScdcWrite(0x20, 3);
  } else {
    ScdcWrite(0x20, 0);
    ScdcWrite(0x20, 0);
  }

  // Setup HDMI related registers in VPU

  // not really needed since we are not converting from 420/422. but set anyways
  VpuHdmiFmtCtrlReg::Get()
      .FromValue(0)
      .set_cntl_chroma_dnsmp(2)
      .set_cntl_hdmi_dith_en(0)
      .set_rounding_enable(1)
      .WriteTo(&(*vpu_mmio_));

  // setup some magic registers
  VpuHdmiDithCntlReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_cntl_hdmi_dith_en(1)
      .set_hsync_invert(0)
      .set_vsync_invert(0)
      .WriteTo(&(*vpu_mmio_));

  // reset vpu bridge
  uint32_t wr_rate = VpuHdmiSettingReg::Get().ReadFrom(&(*vpu_mmio_)).wr_rate();
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_EN, 0);
  VpuHdmiSettingReg::Get()
      .ReadFrom(&(*vpu_mmio_))
      .set_src_sel(0)
      .set_wr_rate(0)
      .WriteTo(&(*vpu_mmio_));
  usleep(1);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_EN, 1);
  usleep(1);
  VpuHdmiSettingReg::Get().ReadFrom(&(*vpu_mmio_)).set_wr_rate(wr_rate).WriteTo(&(*vpu_mmio_));
  usleep(1);
  VpuHdmiSettingReg::Get().ReadFrom(&(*vpu_mmio_)).set_src_sel(2).WriteTo(&(*vpu_mmio_));

  auto regval = ReadReg(HDMITX_DWC_FC_INVIDCONF);
  regval &= ~(1 << 3);  // clear hdmi mode select
  WriteReg(HDMITX_DWC_FC_INVIDCONF, regval);
  usleep(1);
  regval = ReadReg(HDMITX_DWC_FC_INVIDCONF);
  regval |= (1 << 3);  // clear hdmi mode select
  WriteReg(HDMITX_DWC_FC_INVIDCONF, regval);
  usleep(1);

  // setup hdmi phy
  ConfigPhy(&p_);

  DISP_INFO("done!!\n");
  return ZX_OK;
}

void AmlHdmitx::ConfigEncoder(const struct hdmi_param* p) {
  uint32_t h_begin, h_end;
  uint32_t v_begin, v_end;
  uint32_t hs_begin, hs_end;
  uint32_t vs_begin, vs_end;
  uint32_t vsync_adjust = 0;
  uint32_t active_lines, total_lines;
  uint32_t venc_total_pixels, venc_active_pixels, venc_fp, venc_hsync;

  active_lines = (p->timings.vactive / (1 + p->timings.interlace_mode));
  total_lines = (active_lines + p->timings.vblank0) +
                ((active_lines + p->timings.vblank1) * p->timings.interlace_mode);

  venc_total_pixels =
      (p->timings.htotal / (p->timings.pixel_repeat + 1)) * (p->timings.venc_pixel_repeat + 1);

  venc_active_pixels =
      (p->timings.hactive / (p->timings.pixel_repeat + 1)) * (p->timings.venc_pixel_repeat + 1);

  venc_fp =
      (p->timings.hfront / (p->timings.pixel_repeat + 1)) * (p->timings.venc_pixel_repeat + 1);

  venc_hsync =
      (p->timings.hsync / (p->timings.pixel_repeat + 1)) * (p->timings.venc_pixel_repeat + 1);

  SET_BIT32(VPU, VPU_ENCP_VIDEO_MODE, 1, 14, 1);  // DE Signal polarity
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_HAVON_BEGIN, p->timings.hsync + p->timings.hback);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_HAVON_END,
              p->timings.hsync + p->timings.hback + p->timings.hactive - 1);

  WRITE32_REG(VPU, VPU_ENCP_VIDEO_VAVON_BLINE, p->timings.vsync + p->timings.vback);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_VAVON_ELINE,
              p->timings.vsync + p->timings.vback + p->timings.vactive - 1);

  WRITE32_REG(VPU, VPU_ENCP_VIDEO_HSO_BEGIN, 0);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_HSO_END, p->timings.hsync);

  WRITE32_REG(VPU, VPU_ENCP_VIDEO_VSO_BLINE, 0);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_VSO_ELINE, p->timings.vsync);

  // Below calculations assume no pixel repeat and progressive mode.
  // HActive Start/End
  h_begin = p->timings.hsync + p->timings.hback + 2;  // 2 is the HDMI Latency

  h_begin = h_begin % venc_total_pixels;  // wrap around if needed
  h_end = h_begin + venc_active_pixels;
  h_end = h_end % venc_total_pixels;  // wrap around if needed
  WRITE32_REG(VPU, VPU_ENCP_DE_H_BEGIN, h_begin);
  WRITE32_REG(VPU, VPU_ENCP_DE_H_END, h_end);

  // VActive Start/End
  v_begin = p->timings.vsync + p->timings.vback;
  v_end = v_begin + active_lines;
  WRITE32_REG(VPU, VPU_ENCP_DE_V_BEGIN_EVEN, v_begin);
  WRITE32_REG(VPU, VPU_ENCP_DE_V_END_EVEN, v_end);

  if (p->timings.interlace_mode) {
    // TODO: Add support for interlace mode
    // We should not even get here
    DISP_ERROR("Interface mode not supported\n");
  }

  // HSync Timings
  hs_begin = h_end + venc_fp;
  if (hs_begin >= venc_total_pixels) {
    hs_begin -= venc_total_pixels;
    vsync_adjust = 1;
  }

  hs_end = hs_begin + venc_hsync;
  hs_end = hs_end % venc_total_pixels;
  WRITE32_REG(VPU, VPU_ENCP_DVI_HSO_BEGIN, hs_begin);
  WRITE32_REG(VPU, VPU_ENCP_DVI_HSO_END, hs_end);

  // VSync Timings
  if (v_begin >= (p->timings.vback + p->timings.vsync + (1 - vsync_adjust))) {
    vs_begin = v_begin - p->timings.vback - p->timings.vsync - (1 - vsync_adjust);
  } else {
    vs_begin =
        p->timings.vtotal + v_begin - p->timings.vback - p->timings.vsync - (1 - vsync_adjust);
  }
  vs_end = vs_begin + p->timings.vsync;
  vs_end = vs_end % total_lines;

  WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_BLINE_EVN, vs_begin);
  WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_ELINE_EVN, vs_end);
  WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_BEGIN_EVN, hs_begin);
  WRITE32_REG(VPU, VPU_ENCP_DVI_VSO_END_EVN, hs_begin);

  WRITE32_REG(VPU, VPU_HDMI_SETTING, 0);
  // hsync, vsync active high. output CbYCr (GRB)
  // TODO: output desired format is hardcoded here to CbYCr (GRB)
  WRITE32_REG(VPU, VPU_HDMI_SETTING, (p->timings.hpol << 2) | (p->timings.vpol << 3) | (4 << 5));

  if (p->timings.venc_pixel_repeat) {
    SET_BIT32(VPU, VPU_HDMI_SETTING, 1, 8, 1);
  }

  // Select ENCP data to HDMI
  VpuHdmiSettingReg::Get().ReadFrom(&(*vpu_mmio_)).set_src_sel(2).WriteTo(&(*vpu_mmio_));

  DISP_INFO("done\n");
}

void AmlHdmitx::ConfigHdmitx(const struct hdmi_param* p) {
  uint32_t hdmi_data;

  // Output normal TMDS Data
  hdmi_data = (1 << 12);
  WriteReg(HDMITX_TOP_BIST_CNTL, hdmi_data);

  // setup video input mapping
  hdmi_data = 0;
  if (input_color_format_ == HDMI_COLOR_FORMAT_RGB) {
    switch (color_depth_) {
      case HDMI_COLOR_DEPTH_24B:
        hdmi_data |= TX_INVID0_VM_RGB444_8B;
        break;
      case HDMI_COLOR_DEPTH_30B:
        hdmi_data |= TX_INVID0_VM_RGB444_10B;
        break;
      case HDMI_COLOR_DEPTH_36B:
        hdmi_data |= TX_INVID0_VM_RGB444_12B;
        break;
      case HDMI_COLOR_DEPTH_48B:
      default:
        hdmi_data |= TX_INVID0_VM_RGB444_16B;
        break;
    }
  } else if (input_color_format_ == HDMI_COLOR_FORMAT_444) {
    switch (color_depth_) {
      case HDMI_COLOR_DEPTH_24B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_8B;
        break;
      case HDMI_COLOR_DEPTH_30B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_10B;
        break;
      case HDMI_COLOR_DEPTH_36B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_12B;
        break;
      case HDMI_COLOR_DEPTH_48B:
      default:
        hdmi_data |= TX_INVID0_VM_YCBCR444_16B;
        break;
    }
  } else {
    DISP_ERROR("Unsupported format!\n");
    return;
  }
  WriteReg(HDMITX_DWC_TX_INVID0, hdmi_data);

  // Disable video input stuffing and zero-out related registers
  WriteReg(HDMITX_DWC_TX_INSTUFFING, 0x00);
  WriteReg(HDMITX_DWC_TX_GYDATA0, 0x00);
  WriteReg(HDMITX_DWC_TX_GYDATA1, 0x00);
  WriteReg(HDMITX_DWC_TX_RCRDATA0, 0x00);
  WriteReg(HDMITX_DWC_TX_RCRDATA1, 0x00);
  WriteReg(HDMITX_DWC_TX_BCBDATA0, 0x00);
  WriteReg(HDMITX_DWC_TX_BCBDATA1, 0x00);

  // configure CSC (Color Space Converter)
  ConfigCsc(p);

  // Video packet color depth and pixel repetition (none). writing 0 is also valid
  // hdmi_data = (4 << 4); // 4 == 24bit
  // hdmi_data = (display->color_depth << 4); // 4 == 24bit
  hdmi_data = (0 << 4);  // 4 == 24bit
  WriteReg(HDMITX_DWC_VP_PR_CD, hdmi_data);

  // setup video packet stuffing (nothing fancy to be done here)
  hdmi_data = 0;
  WriteReg(HDMITX_DWC_VP_STUFF, hdmi_data);

  // setup video packet remap (nothing here as well since we don't support 422)
  hdmi_data = 0;
  WriteReg(HDMITX_DWC_VP_REMAP, hdmi_data);

  // vp packet output configuration
  // hdmi_data = 0;
  hdmi_data = VP_CONF_BYPASS_EN;
  hdmi_data |= VP_CONF_BYPASS_SEL_VP;
  hdmi_data |= VP_CONF_OUTSELECTOR;
  WriteReg(HDMITX_DWC_VP_CONF, hdmi_data);

  // Video packet Interrupt Mask
  hdmi_data = 0xFF;  // set all bits
  WriteReg(HDMITX_DWC_VP_MASK, hdmi_data);

  // TODO: For now skip audio configuration

  // Setup frame composer

  // fc_invidconf setup
  hdmi_data = 0;
  hdmi_data |= FC_INVIDCONF_HDCP_KEEPOUT;
  hdmi_data |= FC_INVIDCONF_VSYNC_POL(p->timings.vpol);
  hdmi_data |= FC_INVIDCONF_HSYNC_POL(p->timings.hpol);
  hdmi_data |= FC_INVIDCONF_DE_POL_H;
  hdmi_data |= FC_INVIDCONF_DVI_HDMI_MODE;
  if (p->timings.interlace_mode) {
    hdmi_data |= FC_INVIDCONF_VBLANK_OSC | FC_INVIDCONF_IN_VID_INTERLACED;
  }
  WriteReg(HDMITX_DWC_FC_INVIDCONF, hdmi_data);

  // HActive
  hdmi_data = p->timings.hactive;
  WriteReg(HDMITX_DWC_FC_INHACTV0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INHACTV1, ((hdmi_data >> 8) & 0x3f));

  // HBlank
  hdmi_data = p->timings.hblank;
  WriteReg(HDMITX_DWC_FC_INHBLANK0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INHBLANK1, ((hdmi_data >> 8) & 0x1f));

  // VActive
  hdmi_data = p->timings.vactive;
  WriteReg(HDMITX_DWC_FC_INVACTV0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INVACTV1, ((hdmi_data >> 8) & 0x1f));

  // VBlank
  hdmi_data = p->timings.vblank0;
  WriteReg(HDMITX_DWC_FC_INVBLANK, (hdmi_data & 0xff));

  // HFP
  hdmi_data = p->timings.hfront;
  WriteReg(HDMITX_DWC_FC_HSYNCINDELAY0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_HSYNCINDELAY1, ((hdmi_data >> 8) & 0x1f));

  // HSync
  hdmi_data = p->timings.hsync;
  WriteReg(HDMITX_DWC_FC_HSYNCINWIDTH0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_HSYNCINWIDTH1, ((hdmi_data >> 8) & 0x3));

  // VFront
  hdmi_data = p->timings.vfront;
  WriteReg(HDMITX_DWC_FC_VSYNCINDELAY, (hdmi_data & 0xff));

  // VSync
  hdmi_data = p->timings.vsync;
  WriteReg(HDMITX_DWC_FC_VSYNCINWIDTH, (hdmi_data & 0x3f));

  // Frame Composer control period duration (set to 12 per spec)
  WriteReg(HDMITX_DWC_FC_CTRLDUR, 12);

  // Frame Composer extended control period duration (set to 32 per spec)
  WriteReg(HDMITX_DWC_FC_EXCTRLDUR, 32);

  // Frame Composer extended control period max spacing (FIXME: spec says 50, uboot sets to 1)
  WriteReg(HDMITX_DWC_FC_EXCTRLSPAC, 1);

  // Frame Composer preamble filler (from uBoot)

  // Frame Composer GCP packet config
  hdmi_data = (1 << 0);  // set avmute. defauly_phase is 0
  WriteReg(HDMITX_DWC_FC_GCP, hdmi_data);

  // Frame Composer AVI Packet config (set active_format_present bit)
  // aviconf0 populates Table 10 of CEA spec (AVI InfoFrame Data Byte 1)
  // Y1Y0 = 00 for RGB, 10 for 444
  if (output_color_format_ == HDMI_COLOR_FORMAT_RGB) {
    hdmi_data = FC_AVICONF0_RGB;
  } else {
    hdmi_data = FC_AVICONF0_444;
  }
  // A0 = 1 Active Formate present on R3R0
  hdmi_data |= FC_AVICONF0_A0;
  WriteReg(HDMITX_DWC_FC_AVICONF0, hdmi_data);

  // aviconf1 populates Table 11 of AVI InfoFrame Data Byte 2
  // C1C0 = 0, M1M0=0x2 (16:9), R3R2R1R0=0x8 (same of M1M0)
  hdmi_data = FC_AVICONF1_R3R0;  // set to 0x8 (same as coded frame aspect ratio)
  hdmi_data |= FC_AVICONF1_M1M0(p->aspect_ratio);
  hdmi_data |= FC_AVICONF1_C1C0(p->colorimetry);
  WriteReg(HDMITX_DWC_FC_AVICONF1, hdmi_data);

  // Since we are support RGB/444, no need to write to ECx
  WriteReg(HDMITX_DWC_FC_AVICONF2, 0x0);

  // YCC and IT Quantizations according to CEA spec (limited range for now)
  WriteReg(HDMITX_DWC_FC_AVICONF3, 0x0);

  // Set AVI InfoFrame VIC
  // WriteReg(HDMITX_DWC_FC_AVIVID, (p->vic >= VESA_OFFSET)? 0 : p->vic);

  WriteReg(HDMITX_DWC_FC_ACTSPC_HDLR_CFG, 0);

  // Frame composer 2d vact config
  hdmi_data = p->timings.vactive;
  WriteReg(HDMITX_DWC_FC_INVACT_2D_0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INVACT_2D_1, ((hdmi_data >> 8) & 0xf));

  // disable all Frame Composer interrupts
  WriteReg(HDMITX_DWC_FC_MASK0, 0xe7);
  WriteReg(HDMITX_DWC_FC_MASK1, 0xfb);
  WriteReg(HDMITX_DWC_FC_MASK2, 0x3);

  // No pixel repetition for the currently supported resolution
  WriteReg(HDMITX_DWC_FC_PRCONF, ((p->timings.pixel_repeat + 1) << 4) | (p->timings.pixel_repeat)
                                                                            << 0);

  // Skip HDCP for now

  // Clear Interrupts
  WriteReg(HDMITX_DWC_IH_FC_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_FC_STAT1, 0xff);
  WriteReg(HDMITX_DWC_IH_FC_STAT2, 0xff);
  WriteReg(HDMITX_DWC_IH_AS_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_PHY_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_I2CM_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_CEC_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_VP_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_I2CMPHY_STAT0, 0xff);
  WriteReg(HDMITX_DWC_A_APIINTCLR, 0xff);
  WriteReg(HDMITX_DWC_HDCP22REG_STAT, 0xff);

  WriteReg(HDMITX_TOP_INTR_STAT_CLR, 0x0000001f);

  // setup interrupts we care about
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT1, 0xff);
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT2, 0x3);

  WriteReg(HDMITX_DWC_IH_MUTE_AS_STAT0, 0x7);  // mute all

  WriteReg(HDMITX_DWC_IH_MUTE_PHY_STAT0, 0x3f);

  hdmi_data = (1 << 1);  // mute i2c master done.
  WriteReg(HDMITX_DWC_IH_MUTE_I2CM_STAT0, hdmi_data);

  // turn all cec-related interrupts on
  WriteReg(HDMITX_DWC_IH_MUTE_CEC_STAT0, 0x0);

  WriteReg(HDMITX_DWC_IH_MUTE_VP_STAT0, 0xff);

  WriteReg(HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0, 0x03);

  // enable global interrupt
  WriteReg(HDMITX_DWC_IH_MUTE, 0x0);

  WriteReg(HDMITX_TOP_INTR_MASKN, 0x9f);

  // reset
  WriteReg(HDMITX_DWC_MC_SWRSTZREQ, 0x00);
  usleep(10);
  WriteReg(HDMITX_DWC_MC_SWRSTZREQ, 0x7d);
  // why???
  WriteReg(HDMITX_DWC_FC_VSYNCINWIDTH, ReadReg(HDMITX_DWC_FC_VSYNCINWIDTH));

  WriteReg(HDMITX_DWC_MC_CLKDIS, 0);
  WRITE32_REG(VPU, VPU_ENCP_VIDEO_EN, 0xff);

  // dump_regs(display);
  DISP_INFO("done\n");
}

void AmlHdmitx::ConfigPhy(const struct hdmi_param* p) {
  HhiHdmiPhyCntl0Reg::Get().FromValue(0).WriteTo(&(*hhi_mmio_));
  HhiHdmiPhyCntl1Reg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_hdmi_tx_phy_soft_reset(0)
      .set_hdmi_tx_phy_clk_en(0)
      .set_hdmi_fifo_enable(0)
      .set_hdmi_fifo_wr_enable(0)
      .set_msb_lsb_swap(0)
      .set_bit_invert(0)
      .set_ch0_swap(0)
      .set_ch1_swap(1)
      .set_ch2_swap(2)
      .set_ch3_swap(3)
      .set_new_prbs_en(0)
      .set_new_prbs_sel(0)
      .set_new_prbs_prbsmode(0)
      .set_new_prbs_mode(0)
      .WriteTo(&(*hhi_mmio_));

  HhiHdmiPhyCntl1Reg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_hdmi_tx_phy_soft_reset(1)
      .set_hdmi_tx_phy_clk_en(1)
      .set_hdmi_fifo_enable(1)
      .set_hdmi_fifo_wr_enable(1)
      .WriteTo(&(*hhi_mmio_));
  usleep(2);
  HhiHdmiPhyCntl1Reg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_hdmi_tx_phy_soft_reset(0)
      .set_hdmi_tx_phy_clk_en(1)
      .set_hdmi_fifo_enable(1)
      .set_hdmi_fifo_wr_enable(1)
      .WriteTo(&(*hhi_mmio_));
  usleep(2);
  HhiHdmiPhyCntl1Reg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_hdmi_tx_phy_soft_reset(1)
      .set_hdmi_tx_phy_clk_en(1)
      .set_hdmi_fifo_enable(1)
      .set_hdmi_fifo_wr_enable(1)
      .WriteTo(&(*hhi_mmio_));
  usleep(2);
  HhiHdmiPhyCntl1Reg::Get()
      .ReadFrom(&(*hhi_mmio_))
      .set_hdmi_tx_phy_soft_reset(0)
      .set_hdmi_tx_phy_clk_en(1)
      .set_hdmi_fifo_enable(1)
      .set_hdmi_fifo_wr_enable(1)
      .WriteTo(&(*hhi_mmio_));
  usleep(2);

  switch (p->phy_mode) {
    case 1: /* 5.94Gbps, 3.7125Gbsp */
      HhiHdmiPhyCntl0Reg::Get().FromValue(0).set_hdmi_ctl1(0x37eb).set_hdmi_ctl2(0x65c4).WriteTo(
          &(*hhi_mmio_));
      HhiHdmiPhyCntl3Reg::Get().FromValue(0x2ab0ff3b).WriteTo(&(*hhi_mmio_));
      HhiHdmiPhyCntl5Reg::Get().FromValue(0x0000080b).WriteTo(&(*hhi_mmio_));
      break;
    case 2: /* 2.97Gbps */
      HhiHdmiPhyCntl0Reg::Get().FromValue(0).set_hdmi_ctl1(0x33eb).set_hdmi_ctl2(0x6262).WriteTo(
          &(*hhi_mmio_));
      HhiHdmiPhyCntl3Reg::Get().FromValue(0x2ab0ff3b).WriteTo(&(*hhi_mmio_));
      HhiHdmiPhyCntl5Reg::Get().FromValue(0x00000003).WriteTo(&(*hhi_mmio_));
      break;
    default: /* 1.485Gbps, and below */
      HhiHdmiPhyCntl0Reg::Get().FromValue(0).set_hdmi_ctl1(0x33eb).set_hdmi_ctl2(0x4242).WriteTo(
          &(*hhi_mmio_));
      HhiHdmiPhyCntl3Reg::Get().FromValue(0x2ab0ff3b).WriteTo(&(*hhi_mmio_));
      HhiHdmiPhyCntl5Reg::Get().FromValue(0x00000003).WriteTo(&(*hhi_mmio_));
      break;
  }
  usleep(20);
  DISP_INFO("done!\n");
}

void AmlHdmitx::ConfigCsc(const struct hdmi_param* p) {
  uint8_t csc_coef_a1_msb;
  uint8_t csc_coef_a1_lsb;
  uint8_t csc_coef_a2_msb;
  uint8_t csc_coef_a2_lsb;
  uint8_t csc_coef_a3_msb;
  uint8_t csc_coef_a3_lsb;
  uint8_t csc_coef_a4_msb;
  uint8_t csc_coef_a4_lsb;
  uint8_t csc_coef_b1_msb;
  uint8_t csc_coef_b1_lsb;
  uint8_t csc_coef_b2_msb;
  uint8_t csc_coef_b2_lsb;
  uint8_t csc_coef_b3_msb;
  uint8_t csc_coef_b3_lsb;
  uint8_t csc_coef_b4_msb;
  uint8_t csc_coef_b4_lsb;
  uint8_t csc_coef_c1_msb;
  uint8_t csc_coef_c1_lsb;
  uint8_t csc_coef_c2_msb;
  uint8_t csc_coef_c2_lsb;
  uint8_t csc_coef_c3_msb;
  uint8_t csc_coef_c3_lsb;
  uint8_t csc_coef_c4_msb;
  uint8_t csc_coef_c4_lsb;
  uint8_t csc_scale;
  uint32_t hdmi_data;

  if (input_color_format_ == output_color_format_) {
    // no need to convert
    hdmi_data = MC_FLOWCTRL_BYPASS_CSC;
  } else {
    // conversion will be needed
    hdmi_data = MC_FLOWCTRL_ENB_CSC;
  }
  WriteReg(HDMITX_DWC_MC_FLOWCTRL, hdmi_data);

  // Since we don't support 422 at this point, set csc_cfg to 0
  WriteReg(HDMITX_DWC_CSC_CFG, 0);

  // Co-efficient values are from DesignWare Core HDMI TX Video Datapath Application Note V2.1

  // First determine whether we need to convert or not
  if (input_color_format_ != output_color_format_) {
    if (input_color_format_ == HDMI_COLOR_FORMAT_RGB) {
      // from RGB
      csc_coef_a1_msb = 0x25;
      csc_coef_a1_lsb = 0x91;
      csc_coef_a2_msb = 0x13;
      csc_coef_a2_lsb = 0x23;
      csc_coef_a3_msb = 0x07;
      csc_coef_a3_lsb = 0x4C;
      csc_coef_a4_msb = 0x00;
      csc_coef_a4_lsb = 0x00;
      csc_coef_b1_msb = 0xE5;
      csc_coef_b1_lsb = 0x34;
      csc_coef_b2_msb = 0x20;
      csc_coef_b2_lsb = 0x00;
      csc_coef_b3_msb = 0xFA;
      csc_coef_b3_lsb = 0xCC;
      switch (color_depth_) {
        case HDMI_COLOR_DEPTH_24B:
          csc_coef_b4_msb = 0x02;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x02;
          csc_coef_c4_lsb = 0x00;
          break;
        case HDMI_COLOR_DEPTH_30B:
          csc_coef_b4_msb = 0x08;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x08;
          csc_coef_c4_lsb = 0x00;
          break;
        case HDMI_COLOR_DEPTH_36B:
          csc_coef_b4_msb = 0x20;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x20;
          csc_coef_c4_lsb = 0x00;
          break;
        default:
          csc_coef_b4_msb = 0x20;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x20;
          csc_coef_c4_lsb = 0x00;
      }
      csc_coef_c1_msb = 0xEA;
      csc_coef_c1_lsb = 0xCD;
      csc_coef_c2_msb = 0xF5;
      csc_coef_c2_lsb = 0x33;
      csc_coef_c3_msb = 0x20;
      csc_coef_c3_lsb = 0x00;
      csc_scale = 0;
    } else {
      // to RGB
      csc_coef_a1_msb = 0x10;
      csc_coef_a1_lsb = 0x00;
      csc_coef_a2_msb = 0xf4;
      csc_coef_a2_lsb = 0x93;
      csc_coef_a3_msb = 0xfa;
      csc_coef_a3_lsb = 0x7f;
      csc_coef_b1_msb = 0x10;
      csc_coef_b1_lsb = 0x00;
      csc_coef_b2_msb = 0x16;
      csc_coef_b2_lsb = 0x6e;
      csc_coef_b3_msb = 0x00;
      csc_coef_b3_lsb = 0x00;
      switch (color_depth_) {
        case HDMI_COLOR_DEPTH_24B:
          csc_coef_a4_msb = 0x00;
          csc_coef_a4_lsb = 0x87;
          csc_coef_b4_msb = 0xff;
          csc_coef_b4_lsb = 0x4d;
          csc_coef_c4_msb = 0xff;
          csc_coef_c4_lsb = 0x1e;
          break;
        case HDMI_COLOR_DEPTH_30B:
          csc_coef_a4_msb = 0x02;
          csc_coef_a4_lsb = 0x1d;
          csc_coef_b4_msb = 0xfd;
          csc_coef_b4_lsb = 0x33;
          csc_coef_c4_msb = 0xfc;
          csc_coef_c4_lsb = 0x75;
          break;
        case HDMI_COLOR_DEPTH_36B:
          csc_coef_a4_msb = 0x08;
          csc_coef_a4_lsb = 0x77;
          csc_coef_b4_msb = 0xf4;
          csc_coef_b4_lsb = 0xc9;
          csc_coef_c4_msb = 0xf1;
          csc_coef_c4_lsb = 0xd3;
          break;
        default:
          csc_coef_a4_msb = 0x08;
          csc_coef_a4_lsb = 0x77;
          csc_coef_b4_msb = 0xf4;
          csc_coef_b4_lsb = 0xc9;
          csc_coef_c4_msb = 0xf1;
          csc_coef_c4_lsb = 0xd3;
      }
      csc_coef_b4_msb = 0xff;
      csc_coef_b4_lsb = 0x4d;
      csc_coef_c1_msb = 0x10;
      csc_coef_c1_lsb = 0x00;
      csc_coef_c2_msb = 0x00;
      csc_coef_c2_lsb = 0x00;
      csc_coef_c3_msb = 0x1c;
      csc_coef_c3_lsb = 0x5a;
      csc_coef_c4_msb = 0xff;
      csc_coef_c4_lsb = 0x1e;
      csc_scale = 2;
    }
  } else {
    // No conversion. re-write default values just in case
    csc_coef_a1_msb = 0x20;
    csc_coef_a1_lsb = 0x00;
    csc_coef_a2_msb = 0x00;
    csc_coef_a2_lsb = 0x00;
    csc_coef_a3_msb = 0x00;
    csc_coef_a3_lsb = 0x00;
    csc_coef_a4_msb = 0x00;
    csc_coef_a4_lsb = 0x00;
    csc_coef_b1_msb = 0x00;
    csc_coef_b1_lsb = 0x00;
    csc_coef_b2_msb = 0x20;
    csc_coef_b2_lsb = 0x00;
    csc_coef_b3_msb = 0x00;
    csc_coef_b3_lsb = 0x00;
    csc_coef_b4_msb = 0x00;
    csc_coef_b4_lsb = 0x00;
    csc_coef_c1_msb = 0x00;
    csc_coef_c1_lsb = 0x00;
    csc_coef_c2_msb = 0x00;
    csc_coef_c2_lsb = 0x00;
    csc_coef_c3_msb = 0x20;
    csc_coef_c3_lsb = 0x00;
    csc_coef_c4_msb = 0x00;
    csc_coef_c4_lsb = 0x00;
    csc_scale = 1;
  }

  WriteReg(HDMITX_DWC_CSC_COEF_A1_MSB, csc_coef_a1_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_A1_LSB, csc_coef_a1_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_A2_MSB, csc_coef_a2_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_A2_LSB, csc_coef_a2_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_A3_MSB, csc_coef_a3_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_A3_LSB, csc_coef_a3_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_A4_MSB, csc_coef_a4_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_A4_LSB, csc_coef_a4_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_B1_MSB, csc_coef_b1_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_B1_LSB, csc_coef_b1_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_B2_MSB, csc_coef_b2_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_B2_LSB, csc_coef_b2_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_B3_MSB, csc_coef_b3_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_B3_LSB, csc_coef_b3_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_B4_MSB, csc_coef_b4_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_B4_LSB, csc_coef_b4_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_C1_MSB, csc_coef_c1_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_C1_LSB, csc_coef_c1_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_C2_MSB, csc_coef_c2_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_C2_LSB, csc_coef_c2_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_C3_MSB, csc_coef_c3_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_C3_LSB, csc_coef_c3_lsb);
  WriteReg(HDMITX_DWC_CSC_COEF_C4_MSB, csc_coef_c4_msb);
  WriteReg(HDMITX_DWC_CSC_COEF_C4_LSB, csc_coef_c4_lsb);

  hdmi_data = 0;
  hdmi_data |= CSC_SCALE_COLOR_DEPTH(color_depth_);
  hdmi_data |= CSC_SCALE_CSCSCALE(csc_scale);
  WriteReg(HDMITX_DWC_CSC_SCALE, hdmi_data);
}

void AmlHdmitx::ShutDown() {
  /* Close HDMITX PHY */
  WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL0, 0);
  WRITE32_REG(HHI, HHI_HDMI_PHY_CNTL3, 0);
  /* Disable HPLL */
  WRITE32_REG(HHI, HHI_HDMI_PLL_CNTL, 0);
}

zx_status_t AmlHdmitx::I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list,
                                       size_t op_count) {
  if (!op_list) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&i2c_lock_);

  uint8_t segment_num = 0;
  uint8_t offset = 0;
  for (unsigned i = 0; i < op_count; i++) {
    auto op = op_list[i];

    // The HDMITX_DWC_I2CM registers are a limited interface to the i2c bus for the E-DDC
    // protocol, which is good enough for the bus this device provides.
    if (op.address == 0x30 && !op.is_read && op.data_size == 1) {
      segment_num = *((const uint8_t*)op.data_buffer);
    } else if (op.address == 0x50 && !op.is_read && op.data_size == 1) {
      offset = *((const uint8_t*)op.data_buffer);
    } else if (op.address == 0x50 && op.is_read) {
      if (op.data_size % 8 != 0) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      WriteReg(HDMITX_DWC_I2CM_SLAVE, 0x50);
      WriteReg(HDMITX_DWC_I2CM_SEGADDR, 0x30);
      WriteReg(HDMITX_DWC_I2CM_SEGPTR, segment_num);

      for (uint32_t i = 0; i < op.data_size; i += 8) {
        WriteReg(HDMITX_DWC_I2CM_ADDRESS, offset);
        WriteReg(HDMITX_DWC_I2CM_OPERATION, 1 << 2);
        offset = static_cast<uint8_t>(offset + 8);

        uint32_t timeout = 0;
        while ((!(ReadReg(HDMITX_DWC_IH_I2CM_STAT0) & (1 << 1))) && (timeout < 5)) {
          usleep(1000);
          timeout++;
        }
        if (timeout == 5) {
          DISP_ERROR("HDMI DDC TimeOut\n");
          return ZX_ERR_TIMED_OUT;
        }
        usleep(1000);
        WriteReg(HDMITX_DWC_IH_I2CM_STAT0, 1 << 1);  // clear INT

        for (int j = 0; j < 8; j++) {
          uint32_t address = static_cast<uint32_t>(HDMITX_DWC_I2CM_READ_BUFF0 + j);
          ((uint8_t*)op.data_buffer)[i + j] = static_cast<uint8_t>(ReadReg(address));
        }
      }
    } else {
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (op.stop) {
      segment_num = 0;
      offset = 0;
    }
  }

  return ZX_OK;
}

}  // namespace amlogic_display
