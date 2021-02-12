// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_

#include <hwreg/bitfields.h>

#define READ32_VPU_REG(a) vpu_mmio_->Read32(a)
#define WRITE32_VPU_REG(a, v) vpu_mmio_->Write32(v, a)

// Offsets
#define VPU_VENC_VIDEO_TST_EN (0x1b70 << 2)
#define VPU_VENC_VIDEO_TST_MDSEL (0x1b71 << 2)
#define VPU_VENC_VIDEO_TST_Y (0x1b72 << 2)
#define VPU_VENC_VIDEO_TST_CB (0x1b73 << 2)
#define VPU_VENC_VIDEO_TST_CR (0x1b74 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_STRT (0x1b75 << 2)
#define VPU_VENC_VIDEO_TST_CLRBAR_WIDTH (0x1b76 << 2)

#define VPU_ENCP_VIDEO_EN (0x1b80 << 2)
#define VPU_ENCI_VIDEO_EN (0x1b57 << 2)
#define VPU_ENCP_VIDEO_MODE (0x1b8d << 2)
#define VPU_ENCP_VIDEO_MODE_ADV (0x1b8e << 2)
#define VPU_ENCP_VIDEO_MAX_PXCNT (0x1b97 << 2)
#define VPU_ENCP_VIDEO_HAVON_END (0x1ba3 << 2)
#define VPU_ENCP_VIDEO_HAVON_BEGIN (0x1ba4 << 2)
#define VPU_ENCP_VIDEO_VAVON_ELINE (0x1baf << 2)
#define VPU_ENCP_VIDEO_VAVON_BLINE (0x1ba6 << 2)
#define VPU_ENCP_VIDEO_HSO_BEGIN (0x1ba7 << 2)
#define VPU_ENCP_VIDEO_HSO_END (0x1ba8 << 2)
#define VPU_ENCP_VIDEO_VSO_BEGIN (0x1ba9 << 2)
#define VPU_ENCP_VIDEO_VSO_END (0x1baa << 2)
#define VPU_ENCP_VIDEO_VSO_BLINE (0x1bab << 2)
#define VPU_ENCP_VIDEO_VSO_ELINE (0x1bac << 2)
#define VPU_ENCP_VIDEO_MAX_LNCNT (0x1bae << 2)
#define VPU_ENCP_DVI_HSO_BEGIN (0x1c30 << 2)
#define VPU_ENCP_DVI_HSO_END (0x1c31 << 2)
#define VPU_ENCP_DVI_VSO_BLINE_EVN (0x1c32 << 2)
#define VPU_ENCP_DVI_VSO_ELINE_EVN (0x1c34 << 2)
#define VPU_ENCP_DVI_VSO_BEGIN_EVN (0x1c36 << 2)
#define VPU_ENCP_DVI_VSO_END_EVN (0x1c38 << 2)
#define VPU_ENCP_DE_H_BEGIN (0x1c3a << 2)
#define VPU_ENCP_DE_H_END (0x1c3b << 2)
#define VPU_ENCP_DE_V_BEGIN_EVEN (0x1c3c << 2)
#define VPU_ENCP_DE_V_END_EVEN (0x1c3d << 2)
#define VPU_VPU_VIU_VENC_MUX_CTRL (0x271a << 2)
#define VPU_HDMI_SETTING (0x271b << 2)
#define VPU_HDMI_FMT_CTRL (0x2743 << 2)
#define VPU_HDMI_DITH_CNTL (0x27fc << 2)

namespace amlogic_display {

class VpuVpuViuVencMuxCtrlReg : public hwreg::RegisterBase<VpuVpuViuVencMuxCtrlReg, uint32_t> {
 public:
  DEF_FIELD(17, 16, rasp_dpi_clock_sel);
  DEF_FIELD(11, 8, viu_vdin_sel_data);  // 0x0: Disable VIU to VDI6 path
                                        // 0x1: Select ENCI data to VDI6 path
                                        // 0x2: Select ENCP data to VDI6 path
                                        // 0x4: Select ENCT data to VDI6 path
                                        // 0x8: Select ENCL data to VDI6 path
  DEF_FIELD(7, 4, viu_vdin_sel_clk);    // 0x0: Disable VIU to VDI6 clock
                                        // 0x1: Select ENCI clock to VDI6 path
                                        // 0x2: Select ENCP clock to VDI6 path
                                        // 0x4: Select ENCTclock to VDI6 path
                                        // 0x8: Select ENCLclock to VDI6 path
  DEF_FIELD(3, 2, viu2_sel_venc);       // 0: ENCL
                                        // 1: ENCI
                                        // 2: ENCP
                                        // 3: ENCT
  DEF_FIELD(1, 0, viu1_sel_venc);       // 0: ENCL
                                        // 1: ENCI
                                        // 2: ENCP
                                        // 3: ENCT

  static auto Get() {
    return hwreg::RegisterAddr<VpuVpuViuVencMuxCtrlReg>(VPU_VPU_VIU_VENC_MUX_CTRL);
  }
};

class VpuHdmiFmtCtrlReg : public hwreg::RegisterBase<VpuHdmiFmtCtrlReg, uint32_t> {
 public:
  DEF_FIELD(21, 19, frame_count_offset_for_B);
  DEF_FIELD(18, 16, frame_count_offset_for_G);
  DEF_BIT(15, hcnt_hold_when_de_valid);
  DEF_BIT(14, RGB_frame_count_separate);
  DEF_BIT(13, dith4x4_frame_random_enable);
  DEF_BIT(12, dith4x4_enable);
  DEF_BIT(11, tunnel_enable_for_dolby);
  DEF_BIT(10, rounding_enable);
  DEF_FIELD(9, 6, cntl_hdmi_dith10);
  DEF_BIT(5, cntl_hdmi_dith_md);
  DEF_BIT(4, cntl_hdmi_dith_en);
  DEF_FIELD(3, 2, cntl_chroma_dnsmp);  // 0: use pixel 0
                                       // 1: use pixel 1
                                       // 2: use average
  DEF_FIELD(1, 0, cntl_hdmi_vid_fmt);  // 0: no conversion
                                       // 1: convert to 422
                                       // 2: convert to 420

  static auto Get() { return hwreg::RegisterAddr<VpuHdmiFmtCtrlReg>(VPU_HDMI_FMT_CTRL); }
};

class VpuHdmiDithCntlReg : public hwreg::RegisterBase<VpuHdmiDithCntlReg, uint32_t> {
 public:
  DEF_FIELD(21, 19, frame_count_offset_for_B);
  DEF_FIELD(18, 16, frame_count_offset_for_G);
  DEF_BIT(15, hcnt_hold_when_de_valid);
  DEF_BIT(14, RGB_frame_count_separate);
  DEF_BIT(13, dith4x4_frame_random_enable);
  DEF_BIT(12, dith4x4_enable);
  DEF_BIT(11, tunnel_enable_for_dolby);
  DEF_BIT(10, rounding_enable);
  DEF_FIELD(9, 6, cntl_hdmi_dith10);
  DEF_BIT(5, cntl_hdmi_dith_md);
  DEF_BIT(4, cntl_hdmi_dith_en);
  DEF_BIT(3, hsync_invert);
  DEF_BIT(2, vsync_invert);
  DEF_BIT(0, dither_lut_sel);  // 1: sel 10b to 8b
                               // 0: sel 12b to 10b

  static auto Get() { return hwreg::RegisterAddr<VpuHdmiDithCntlReg>(VPU_HDMI_DITH_CNTL); }
};

class VpuHdmiSettingReg : public hwreg::RegisterBase<VpuHdmiSettingReg, uint32_t> {
 public:
  DEF_FIELD(15, 12, rd_rate);      // 0: One read every rd_clk
                                   // 1: One read every 2 rd_clk
                                   // 2: One read every 3 rd_clk
                                   // ...
                                   // 15: One read every 16 rd_clk
  DEF_FIELD(11, 8, wr_rate);       // 0: One write every wd_clk
                                   // 1: One write every 2 wd_clk
                                   // 2: One write every 3 wd_clk
                                   // ...
                                   // 15: One write every 16 wd_clk
  DEF_FIELD(7, 5, data_comp_map);  // 0: output CrYCb (BRG)
                                   // 1: output YCbCr (RGB)
                                   // 2: output YCrCb (RBG)
                                   // 3: output CbCrY (GBR)
                                   // 4: output CbYCr (GRB)
                                   // 5: output CrCbY (BGR)
  DEF_BIT(4, inv_dvi_clk);
  DEF_BIT(3, inv_vsync);
  DEF_BIT(2, inv_hsync);
  DEF_FIELD(1, 0, src_sel);  // 00: disable HDMI source
                             // 01: select ENCI data to HDMI
                             // 10: select ENCP data to HDMI

  static auto Get() { return hwreg::RegisterAddr<VpuHdmiSettingReg>(VPU_HDMI_SETTING); }
};

}  // namespace amlogic_display

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_AMLOGIC_DISPLAY_HDMITX_VPU_REGS_H_
