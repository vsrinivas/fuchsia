// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/hdmi-dw/hdmi-dw.h>
#include <lib/hdmi-dw/regs.h>

namespace hdmi_dw {

using fuchsia_hardware_hdmi::wire::ColorDepth;
using fuchsia_hardware_hdmi::wire::ColorFormat;
using fuchsia_hardware_hdmi::wire::ModeFlag;

void HdmiDw::ScdcWrite(uint8_t addr, uint8_t val) {
  WriteReg(HDMITX_DWC_I2CM_SLAVE, 0x54);
  WriteReg(HDMITX_DWC_I2CM_ADDRESS, addr);
  WriteReg(HDMITX_DWC_I2CM_DATAO, val);
  WriteReg(HDMITX_DWC_I2CM_OPERATION, 0x10);
  usleep(2000);
}

void HdmiDw::ScdcRead(uint8_t addr, uint8_t* val) {
  WriteReg(HDMITX_DWC_I2CM_SLAVE, 0x54);
  WriteReg(HDMITX_DWC_I2CM_ADDRESS, addr);
  WriteReg(HDMITX_DWC_I2CM_OPERATION, 1);
  usleep(2000);
  *val = (uint8_t)ReadReg(HDMITX_DWC_I2CM_DATAI);
}

zx_status_t HdmiDw::InitHw() {
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

  return ZX_OK;
}

void HdmiDw::ConfigHdmitx(const DisplayMode& mode, const hdmi_param_tx& p) {
  // setup video input mapping
  uint32_t hdmi_data = 0;
  if (mode.color().input_color_format == ColorFormat::kCfRgb) {
    switch (mode.color().color_depth) {
      case ColorDepth::kCd24B:
        hdmi_data |= TX_INVID0_VM_RGB444_8B;
        break;
      case ColorDepth::kCd30B:
        hdmi_data |= TX_INVID0_VM_RGB444_10B;
        break;
      case ColorDepth::kCd36B:
        hdmi_data |= TX_INVID0_VM_RGB444_12B;
        break;
      case ColorDepth::kCd48B:
      default:
        hdmi_data |= TX_INVID0_VM_RGB444_16B;
        break;
    }
  } else if (mode.color().input_color_format == ColorFormat::kCf444) {
    switch (mode.color().color_depth) {
      case ColorDepth::kCd24B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_8B;
        break;
      case ColorDepth::kCd30B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_10B;
        break;
      case ColorDepth::kCd36B:
        hdmi_data |= TX_INVID0_VM_YCBCR444_12B;
        break;
      case ColorDepth::kCd48B:
      default:
        hdmi_data |= TX_INVID0_VM_YCBCR444_16B;
        break;
    }
  } else {
    zxlogf(ERROR, "Unsupported format!\n");
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
  ConfigCsc(mode);

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
  hdmi_data |= FC_INVIDCONF_VSYNC_POL(mode->flags & ModeFlag::kVsyncPositive);
  hdmi_data |= FC_INVIDCONF_HSYNC_POL(mode->flags & ModeFlag::kHsyncPositive);
  hdmi_data |= FC_INVIDCONF_DE_POL_H;
  hdmi_data |= FC_INVIDCONF_DVI_HDMI_MODE;
  if (mode.mode().flags & static_cast<uint32_t>(ModeFlag::kInterlaced)) {
    hdmi_data |= FC_INVIDCONF_VBLANK_OSC | FC_INVIDCONF_IN_VID_INTERLACED;
  }
  WriteReg(HDMITX_DWC_FC_INVIDCONF, hdmi_data);

  // HActive
  hdmi_data = mode.mode().h_addressable;
  WriteReg(HDMITX_DWC_FC_INHACTV0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INHACTV1, ((hdmi_data >> 8) & 0x3f));

  // HBlank
  hdmi_data = mode.mode().h_blanking;
  WriteReg(HDMITX_DWC_FC_INHBLANK0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INHBLANK1, ((hdmi_data >> 8) & 0x1f));

  // VActive
  hdmi_data = mode.mode().v_addressable;
  WriteReg(HDMITX_DWC_FC_INVACTV0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INVACTV1, ((hdmi_data >> 8) & 0x1f));

  // VBlank
  hdmi_data = mode.mode().v_blanking;
  WriteReg(HDMITX_DWC_FC_INVBLANK, (hdmi_data & 0xff));

  // HFP
  hdmi_data = mode.mode().h_front_porch;
  WriteReg(HDMITX_DWC_FC_HSYNCINDELAY0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_HSYNCINDELAY1, ((hdmi_data >> 8) & 0x1f));

  // HSync
  hdmi_data = mode.mode().h_sync_pulse;
  WriteReg(HDMITX_DWC_FC_HSYNCINWIDTH0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_HSYNCINWIDTH1, ((hdmi_data >> 8) & 0x3));

  // VFront
  hdmi_data = mode.mode().v_front_porch;
  WriteReg(HDMITX_DWC_FC_VSYNCINDELAY, (hdmi_data & 0xff));

  // VSync
  hdmi_data = mode.mode().v_sync_pulse;
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
  if (mode.color().output_color_format == ColorFormat::kCfRgb) {
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
  hdmi_data |= FC_AVICONF1_M1M0(static_cast<uint8_t>(p.aspect_ratio));
  hdmi_data |= FC_AVICONF1_C1C0(static_cast<uint8_t>(p.colorimetry));
  WriteReg(HDMITX_DWC_FC_AVICONF1, hdmi_data);

  // Since we are support RGB/444, no need to write to ECx
  WriteReg(HDMITX_DWC_FC_AVICONF2, 0x0);

  // YCC and IT Quantizations according to CEA spec (limited range for now)
  WriteReg(HDMITX_DWC_FC_AVICONF3, 0x0);

  // Set AVI InfoFrame VIC
  // WriteReg(HDMITX_DWC_FC_AVIVID, (p->vic >= VESA_OFFSET)? 0 : p->vic);

  WriteReg(HDMITX_DWC_FC_ACTSPC_HDLR_CFG, 0);

  // Frame composer 2d vact config
  hdmi_data = mode.mode().v_addressable;
  WriteReg(HDMITX_DWC_FC_INVACT_2D_0, (hdmi_data & 0xff));
  WriteReg(HDMITX_DWC_FC_INVACT_2D_1, ((hdmi_data >> 8) & 0xf));

  // disable all Frame Composer interrupts
  WriteReg(HDMITX_DWC_FC_MASK0, 0xe7);
  WriteReg(HDMITX_DWC_FC_MASK1, 0xfb);
  WriteReg(HDMITX_DWC_FC_MASK2, 0x3);

  // No pixel repetition for the currently supported resolution
  // TODO: pixel repetition is 0 for most progressive. We don't support interlaced
  uint8_t pixel_repeat = 0;
  WriteReg(HDMITX_DWC_FC_PRCONF, ((pixel_repeat + 1) << 4) | (pixel_repeat) << 0);

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
}

void HdmiDw::SetupInterrupts() {
  // setup interrupts we care about
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT0, 0xff);
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT1, 0xff);
  WriteReg(HDMITX_DWC_IH_MUTE_FC_STAT2, 0x3);

  WriteReg(HDMITX_DWC_IH_MUTE_AS_STAT0, 0x7);  // mute all

  WriteReg(HDMITX_DWC_IH_MUTE_PHY_STAT0, 0x3f);

  WriteReg(HDMITX_DWC_IH_MUTE_I2CM_STAT0, 1 << 1);

  // turn all cec-related interrupts on
  WriteReg(HDMITX_DWC_IH_MUTE_CEC_STAT0, 0x0);

  WriteReg(HDMITX_DWC_IH_MUTE_VP_STAT0, 0xff);

  WriteReg(HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0, 0x03);

  // enable global interrupt
  WriteReg(HDMITX_DWC_IH_MUTE, 0x0);
}

void HdmiDw::Reset() {
  // reset
  WriteReg(HDMITX_DWC_MC_SWRSTZREQ, 0x00);
  usleep(10);
  WriteReg(HDMITX_DWC_MC_SWRSTZREQ, 0x7d);
  // why???
  WriteReg(HDMITX_DWC_FC_VSYNCINWIDTH, ReadReg(HDMITX_DWC_FC_VSYNCINWIDTH));

  WriteReg(HDMITX_DWC_MC_CLKDIS, 0);
}

void HdmiDw::SetupScdc(bool is4k) {
  uint8_t scdc_data = 0;
  ScdcRead(0x1, &scdc_data);
  zxlogf(INFO, "version is %s\n", (scdc_data == 1) ? "2.0" : "<= 1.4");
  // scdc write is done twice in uboot
  // TODO: find scdc register def
  ScdcWrite(0x2, 0x1);
  ScdcWrite(0x2, 0x1);

  if (is4k) {
    ScdcWrite(0x20, 3);
    ScdcWrite(0x20, 3);
  } else {
    ScdcWrite(0x20, 0);
    ScdcWrite(0x20, 0);
  }
}

void HdmiDw::ResetFc() {
  auto regval = ReadReg(HDMITX_DWC_FC_INVIDCONF);
  regval &= ~(1 << 3);  // clear hdmi mode select
  WriteReg(HDMITX_DWC_FC_INVIDCONF, regval);
  usleep(1);
  regval = ReadReg(HDMITX_DWC_FC_INVIDCONF);
  regval |= (1 << 3);  // clear hdmi mode select
  WriteReg(HDMITX_DWC_FC_INVIDCONF, regval);
  usleep(1);
}

void HdmiDw::SetFcScramblerCtrl(bool is4k) {
  if (is4k) {
    // Set
    WriteReg(HDMITX_DWC_FC_SCRAMBLER_CTRL, ReadReg(HDMITX_DWC_FC_SCRAMBLER_CTRL) | (1 << 0));
  } else {
    // Clear
    WriteReg(HDMITX_DWC_FC_SCRAMBLER_CTRL, 0);
  }
}

void HdmiDw::ConfigCsc(const DisplayMode& mode) {
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

  if (mode.color().input_color_format == mode.color().output_color_format) {
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
  if (mode.color().input_color_format != mode.color().output_color_format) {
    if (mode.color().input_color_format == ColorFormat::kCfRgb) {
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
      switch (mode.color().color_depth) {
        case ColorDepth::kCd24B:
          csc_coef_b4_msb = 0x02;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x02;
          csc_coef_c4_lsb = 0x00;
          break;
        case ColorDepth::kCd30B:
          csc_coef_b4_msb = 0x08;
          csc_coef_b4_lsb = 0x00;
          csc_coef_c4_msb = 0x08;
          csc_coef_c4_lsb = 0x00;
          break;
        case ColorDepth::kCd36B:
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
      switch (mode.color().color_depth) {
        case ColorDepth::kCd24B:
          csc_coef_a4_msb = 0x00;
          csc_coef_a4_lsb = 0x87;
          csc_coef_b4_msb = 0xff;
          csc_coef_b4_lsb = 0x4d;
          csc_coef_c4_msb = 0xff;
          csc_coef_c4_lsb = 0x1e;
          break;
        case ColorDepth::kCd30B:
          csc_coef_a4_msb = 0x02;
          csc_coef_a4_lsb = 0x1d;
          csc_coef_b4_msb = 0xfd;
          csc_coef_b4_lsb = 0x33;
          csc_coef_c4_msb = 0xfc;
          csc_coef_c4_lsb = 0x75;
          break;
        case ColorDepth::kCd36B:
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
  hdmi_data |= CSC_SCALE_COLOR_DEPTH(static_cast<uint8_t>(mode.color().color_depth));
  hdmi_data |= CSC_SCALE_CSCSCALE(csc_scale);
  WriteReg(HDMITX_DWC_CSC_SCALE, hdmi_data);
}

zx_status_t HdmiDw::EdidTransfer(const i2c_impl_op_t* op_list, size_t op_count) {
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
          zxlogf(ERROR, "HDMI DDC TimeOut\n");
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

#define PRINT_REG(name) PrintReg(#name, (name))
void HdmiDw::PrintReg(std::string name, uint8_t reg) {
  zxlogf(INFO, "%s (0x%4x): %u", &name[0], reg, ReadReg(reg));
}

void HdmiDw::PrintRegisters() {
  zxlogf(INFO, "------------HdmiDw Registers------------");

  PRINT_REG(HDMITX_DWC_A_APIINTCLR);
  PRINT_REG(HDMITX_DWC_CSC_CFG);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A1_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A1_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A2_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A2_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A3_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A3_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A4_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_A4_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B1_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B1_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B2_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B2_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B3_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B3_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B4_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_B4_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C1_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C1_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C2_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C2_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C3_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C3_LSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C4_MSB);
  PRINT_REG(HDMITX_DWC_CSC_COEF_C4_LSB);
  PRINT_REG(HDMITX_DWC_CSC_SCALE);
  PRINT_REG(HDMITX_DWC_FC_ACTSPC_HDLR_CFG);
  PRINT_REG(HDMITX_DWC_FC_AVICONF0);
  PRINT_REG(HDMITX_DWC_FC_AVICONF1);
  PRINT_REG(HDMITX_DWC_FC_AVICONF2);
  PRINT_REG(HDMITX_DWC_FC_AVICONF3);
  PRINT_REG(HDMITX_DWC_FC_CTRLDUR);
  PRINT_REG(HDMITX_DWC_FC_EXCTRLDUR);
  PRINT_REG(HDMITX_DWC_FC_EXCTRLSPAC);
  PRINT_REG(HDMITX_DWC_FC_GCP);
  PRINT_REG(HDMITX_DWC_FC_HSYNCINDELAY0);
  PRINT_REG(HDMITX_DWC_FC_HSYNCINDELAY1);
  PRINT_REG(HDMITX_DWC_FC_HSYNCINWIDTH0);
  PRINT_REG(HDMITX_DWC_FC_HSYNCINWIDTH1);
  PRINT_REG(HDMITX_DWC_FC_INHACTV0);
  PRINT_REG(HDMITX_DWC_FC_INHACTV1);
  PRINT_REG(HDMITX_DWC_FC_INHBLANK0);
  PRINT_REG(HDMITX_DWC_FC_INHBLANK1);
  PRINT_REG(HDMITX_DWC_FC_INVACTV0);
  PRINT_REG(HDMITX_DWC_FC_INVACTV1);
  PRINT_REG(HDMITX_DWC_FC_INVACT_2D_0);
  PRINT_REG(HDMITX_DWC_FC_INVACT_2D_1);
  PRINT_REG(HDMITX_DWC_FC_INVBLANK);
  PRINT_REG(HDMITX_DWC_FC_INVIDCONF);
  PRINT_REG(HDMITX_DWC_FC_MASK0);
  PRINT_REG(HDMITX_DWC_FC_MASK1);
  PRINT_REG(HDMITX_DWC_FC_MASK2);
  PRINT_REG(HDMITX_DWC_FC_PRCONF);
  PRINT_REG(HDMITX_DWC_FC_SCRAMBLER_CTRL);
  PRINT_REG(HDMITX_DWC_FC_VSYNCINDELAY);
  PRINT_REG(HDMITX_DWC_FC_VSYNCINWIDTH);
  PRINT_REG(HDMITX_DWC_HDCP22REG_STAT);
  PRINT_REG(HDMITX_DWC_I2CM_CTLINT);
  PRINT_REG(HDMITX_DWC_I2CM_DIV);
  PRINT_REG(HDMITX_DWC_I2CM_FS_SCL_HCNT_1);
  PRINT_REG(HDMITX_DWC_I2CM_FS_SCL_HCNT_0);
  PRINT_REG(HDMITX_DWC_I2CM_FS_SCL_LCNT_1);
  PRINT_REG(HDMITX_DWC_I2CM_FS_SCL_LCNT_0);
  PRINT_REG(HDMITX_DWC_I2CM_INT);
  PRINT_REG(HDMITX_DWC_I2CM_SDA_HOLD);
  PRINT_REG(HDMITX_DWC_I2CM_SCDC_UPDATE);
  PRINT_REG(HDMITX_DWC_I2CM_SS_SCL_HCNT_1);
  PRINT_REG(HDMITX_DWC_I2CM_SS_SCL_HCNT_0);
  PRINT_REG(HDMITX_DWC_I2CM_SS_SCL_LCNT_1);
  PRINT_REG(HDMITX_DWC_I2CM_SS_SCL_LCNT_0);
  PRINT_REG(HDMITX_DWC_IH_AS_STAT0);
  PRINT_REG(HDMITX_DWC_IH_CEC_STAT0);
  PRINT_REG(HDMITX_DWC_IH_FC_STAT0);
  PRINT_REG(HDMITX_DWC_IH_FC_STAT1);
  PRINT_REG(HDMITX_DWC_IH_FC_STAT2);
  PRINT_REG(HDMITX_DWC_IH_I2CM_STAT0);
  PRINT_REG(HDMITX_DWC_IH_I2CMPHY_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE);
  PRINT_REG(HDMITX_DWC_IH_MUTE_AS_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_CEC_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_FC_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_FC_STAT1);
  PRINT_REG(HDMITX_DWC_IH_MUTE_FC_STAT2);
  PRINT_REG(HDMITX_DWC_IH_MUTE_I2CM_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_PHY_STAT0);
  PRINT_REG(HDMITX_DWC_IH_MUTE_VP_STAT0);
  PRINT_REG(HDMITX_DWC_IH_PHY_STAT0);
  PRINT_REG(HDMITX_DWC_IH_VP_STAT0);
  PRINT_REG(HDMITX_DWC_MC_FLOWCTRL);
  PRINT_REG(HDMITX_DWC_MC_SWRSTZREQ);
  PRINT_REG(HDMITX_DWC_MC_CLKDIS);
  PRINT_REG(HDMITX_DWC_TX_INVID0);
  PRINT_REG(HDMITX_DWC_TX_INSTUFFING);
  PRINT_REG(HDMITX_DWC_TX_GYDATA0);
  PRINT_REG(HDMITX_DWC_TX_GYDATA1);
  PRINT_REG(HDMITX_DWC_TX_RCRDATA0);
  PRINT_REG(HDMITX_DWC_TX_RCRDATA1);
  PRINT_REG(HDMITX_DWC_TX_BCBDATA0);
  PRINT_REG(HDMITX_DWC_TX_BCBDATA1);
  PRINT_REG(HDMITX_DWC_VP_CONF);
  PRINT_REG(HDMITX_DWC_VP_MASK);
  PRINT_REG(HDMITX_DWC_VP_PR_CD);
  PRINT_REG(HDMITX_DWC_VP_REMAP);
  PRINT_REG(HDMITX_DWC_VP_STUFF);
}
#undef PRINT_REG

}  // namespace hdmi_dw
