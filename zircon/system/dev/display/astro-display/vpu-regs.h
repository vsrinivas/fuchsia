// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_VPU_REGS_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_VPU_REGS_H_
#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

#define VPU_VIU_OSD1_CTRL_STAT (0x1a10 << 2)
#define VPU_VIU_OSD1_CTRL_STAT2 (0x1a2d << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W0 (0x1a1b << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W1 (0x1a1c << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W2 (0x1a1d << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W3 (0x1a1e << 2)
#define VPU_VIU_OSD1_BLK0_CFG_W4 (0x1a13 << 2)
#define VPU_VIU_OSD1_FIFO_CTRL_STAT (0x1a2b << 2)
#define VPU_VIU_OSD2_CTRL_STAT (0x1a30 << 2)
#define VPU_VIU_OSD2_FIFO_CTRL_STAT (0x1a4b << 2)
#define VPU_VIU_OSD2_BLK0_CFG_W4 (0x1a64 << 2)
#define VPU_VIU_OSD_BLEND_CTRL (0x39b0 << 2)
#define VPU_VIU_OSD_BLEND_DIN0_SCOPE_H (0x39b1 << 2)
#define VPU_VIU_OSD_BLEND_DIN0_SCOPE_V (0x39b2 << 2)
#define VPU_VIU_OSD_BLEND_DIN1_SCOPE_H (0x39b3 << 2)
#define VPU_VIU_OSD_BLEND_DIN1_SCOPE_V (0x39b4 << 2)
#define VPU_VIU_OSD_BLEND_DIN2_SCOPE_H (0x39b5 << 2)
#define VPU_VIU_OSD_BLEND_DIN2_SCOPE_V (0x39b6 << 2)
#define VPU_VIU_OSD_BLEND_DIN3_SCOPE_H (0x39b7 << 2)
#define VPU_VIU_OSD_BLEND_DIN3_SCOPE_V (0x39b8 << 2)
#define VPU_VIU_OSD_BLEND_DUMMY_DATA0 (0x39b9 << 2)
#define VPU_VIU_OSD_BLEND_DUMMY_ALPHA (0x39ba << 2)
#define VPU_VIU_OSD_BLEND_BLEND0_SIZE (0x39bb << 2)
#define VPU_VIU_OSD_BLEND_BLEND1_SIZE (0x39bc << 2)
#define VPU_VPP_POSTBLEND_H_SIZE (0x1d21 << 2)
#define VPU_VPP_HOLD_LINES (0x1d22 << 2)
#define VPU_VPP_MISC (0x1d26 << 2)
#define VPU_VPP_OFIFO_SIZE (0x1d27 << 2)
#define VPU_VPP_OUT_H_V_SIZE (0x1da5 << 2)
#define VPU_VPP_OSD_VSC_PHASE_STEP (0x1dc0 << 2)
#define VPU_VPP_OSD_VSC_INI_PHASE (0x1dc1 << 2)
#define VPU_VPP_OSD_VSC_CTRL0 (0x1dc2 << 2)
#define VPU_VPP_OSD_HSC_CTRL0 (0x1dc5 << 2)
#define VPU_VPP_OSD_HSC_PHASE_STEP (0x1dc3 << 2)
#define VPU_VPP_OSD_HSC_INI_PHASE (0x1dc4 << 2)
#define VPU_VPP_OSD_SC_CTRL0 (0x1dc8 << 2)
#define VPU_VPP_OSD_SCI_WH_M1 (0x1dc9 << 2)
#define VPU_VPP_OSD_SCO_H_START_END (0x1dca << 2)
#define VPU_VPP_OSD_SCO_V_START_END (0x1dcb << 2)
#define VPU_VPP_OSD_SCALE_COEF_IDX (0x1dcc << 2)
#define VPU_VPP_OSD_SCALE_COEF (0x1dcd << 2)
#define VPU_VPP_OSD1_IN_SIZE (0x1df1 << 2)
#define VPU_VPP_OSD1_BLD_H_SCOPE (0x1df5 << 2)
#define VPU_VPP_OSD1_BLD_V_SCOPE (0x1df6 << 2)
#define VPU_VPP_OSD2_BLD_H_SCOPE (0x1df7 << 2)
#define VPU_VPP_OSD2_BLD_V_SCOPE (0x1df8 << 2)
#define VPU_OSD_PATH_MISC_CTRL (0x1a0e << 2)
#define VPU_OSD1_BLEND_SRC_CTRL (0x1dfd << 2)
#define VPU_OSD2_BLEND_SRC_CTRL (0x1dfe << 2)
#define VPU_VIU_VENC_MUX_CTRL (0x271a << 2)
#define VPU_RDARB_MODE_L1C1 (0x2790 << 2)
#define VPU_RDARB_MODE_L1C2 (0x2799 << 2)
#define VPU_RDARB_MODE_L2C1 (0x279d << 2)
#define VPU_WRARB_MODE_L2C1 (0x27a2 << 2)

#define VPU_VPP_POST_MATRIX_COEF00_01 (0x32b0 << 2)
#define VPU_VPP_POST_MATRIX_COEF02_10 (0x32b1 << 2)
#define VPU_VPP_POST_MATRIX_COEF11_12 (0x32b2 << 2)
#define VPU_VPP_POST_MATRIX_COEF20_21 (0x32b3 << 2)
#define VPU_VPP_POST_MATRIX_COEF22 (0x32b4 << 2)
#define VPU_VPP_POST_MATRIX_OFFSET0_1 (0x32b9 << 2)
#define VPU_VPP_POST_MATRIX_OFFSET2 (0x32ba << 2)
#define VPU_VPP_POST_MATRIX_PRE_OFFSET0_1 (0x32bb << 2)
#define VPU_VPP_POST_MATRIX_PRE_OFFSET2 (0x32bc << 2)
#define VPU_VPP_POST_MATRIX_EN_CTRL (0x32bd << 2)

// Registers needed for Video Loopback mode
#define VPU_WR_BACK_MISC_CTRL (0x1a0d << 2)
#define VPU_WRBACK_CTRL (0x1df9 << 2)
#define VPU_VDIN0_WRARB_REQEN_SLV (0x12c1 << 2)
#define VPU_VDIN1_COM_CTRL0 (0x1302 << 2)
#define VPU_VDIN1_COM_STATUS0 (0x1305 << 2)
#define VPU_VDIN1_MATRIX_CTRL (0x1310 << 2)
#define VPU_VDIN1_COEF00_01 (0x1311 << 2)
#define VPU_VDIN1_COEF02_10 (0x1312 << 2)
#define VPU_VDIN1_COEF11_12 (0x1313 << 2)
#define VPU_VDIN1_COEF20_21 (0x1314 << 2)
#define VPU_VDIN1_COEF22 (0x1315 << 2)
#define VPU_VDIN1_OFFSET0_1 (0x1316 << 2)
#define VPU_VDIN1_OFFSET2 (0x1317 << 2)
#define VPU_VDIN1_PRE_OFFSET0_1 (0x1318 << 2)
#define VPU_VDIN1_PRE_OFFSET2 (0x1319 << 2)
#define VPU_VDIN1_LFIFO_CTRL (0x131a << 2)
#define VPU_VDIN1_INTF_WIDTHM1 (0x131c << 2)
#define VPU_VDIN1_WR_CTRL2 (0x131f << 2)
#define VPU_VDIN1_WR_CTRL (0x1320 << 2)
#define VPU_VDIN1_WR_H_START_END (0x1321 << 2)
#define VPU_VDIN1_WR_V_START_END (0x1322 << 2)
#define VPU_VDIN1_ASFIFO_CTRL3 (0x136f << 2)
#define VPU_VDIN1_MISC_CTRL (0x2782 << 2)
#define VPU_VIU_VDIN_IF_MUX_CTRL (0x2783 << 2)  // undocumented ¯\_(ツ)_/¯

namespace astro_display {

class WrBackMiscCtrlReg : public hwreg::RegisterBase<WrBackMiscCtrlReg, uint32_t> {
 public:
  DEF_BIT(1, chan1_hsync_enable);
  DEF_BIT(0, chan0_hsync_enable);
  static auto Get() { return hwreg::RegisterAddr<WrBackMiscCtrlReg>(VPU_WR_BACK_MISC_CTRL); }
};

class WrBackCtrlReg : public hwreg::RegisterBase<WrBackCtrlReg, uint32_t> {
 public:
  DEF_FIELD(2, 0, chan0_sel);
  static auto Get() { return hwreg::RegisterAddr<WrBackCtrlReg>(VPU_WRBACK_CTRL); }
};

class VdInComCtrl0Reg : public hwreg::RegisterBase<VdInComCtrl0Reg, uint32_t> {
 public:
  DEF_FIELD(26, 20, hold_lines);
  DEF_BIT(4, enable_vdin);
  DEF_FIELD(3, 0, vdin_selection);
  static auto Get() { return hwreg::RegisterAddr<VdInComCtrl0Reg>(VPU_VDIN1_COM_CTRL0); }
};

class VdInComStatus0Reg : public hwreg::RegisterBase<VdInComStatus0Reg, uint32_t> {
 public:
  DEF_BIT(2, done);
  static auto Get() { return hwreg::RegisterAddr<VdInComStatus0Reg>(VPU_VDIN1_COM_STATUS0); }
};

class VdInMatrixCtrlReg : public hwreg::RegisterBase<VdInMatrixCtrlReg, uint32_t> {
 public:
  DEF_FIELD(3, 2, select);
  DEF_BIT(1, enable);
  static auto Get() { return hwreg::RegisterAddr<VdInMatrixCtrlReg>(VPU_VDIN1_MATRIX_CTRL); }
};

class VdinCoef00_01Reg : public hwreg::RegisterBase<VdinCoef00_01Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef00);
  DEF_FIELD(12, 0, coef01);
  static auto Get() { return hwreg::RegisterAddr<VdinCoef00_01Reg>(VPU_VDIN1_COEF00_01); }
};

class VdinCoef02_10Reg : public hwreg::RegisterBase<VdinCoef02_10Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef02);
  DEF_FIELD(12, 0, coef10);
  static auto Get() { return hwreg::RegisterAddr<VdinCoef02_10Reg>(VPU_VDIN1_COEF02_10); }
};

class VdinCoef11_12Reg : public hwreg::RegisterBase<VdinCoef11_12Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef11);
  DEF_FIELD(12, 0, coef12);
  static auto Get() { return hwreg::RegisterAddr<VdinCoef11_12Reg>(VPU_VDIN1_COEF11_12); }
};

class VdinCoef20_21Reg : public hwreg::RegisterBase<VdinCoef20_21Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, coef20);
  DEF_FIELD(12, 0, coef21);
  static auto Get() { return hwreg::RegisterAddr<VdinCoef20_21Reg>(VPU_VDIN1_COEF20_21); }
};

class VdinCoef22Reg : public hwreg::RegisterBase<VdinCoef22Reg, uint32_t> {
 public:
  DEF_FIELD(12, 0, coef22);
  static auto Get() { return hwreg::RegisterAddr<VdinCoef22Reg>(VPU_VDIN1_COEF22); }
};

class VdinOffset0_1Reg : public hwreg::RegisterBase<VdinOffset0_1Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, offset0);
  DEF_FIELD(12, 0, offset1);
  static auto Get() { return hwreg::RegisterAddr<VdinOffset0_1Reg>(VPU_VDIN1_OFFSET0_1); }
};

class VdinOffset2Reg : public hwreg::RegisterBase<VdinOffset2Reg, uint32_t> {
 public:
  DEF_FIELD(12, 0, offset2);
  static auto Get() { return hwreg::RegisterAddr<VdinOffset2Reg>(VPU_VDIN1_OFFSET2); }
};

class VdinPreOffset0_1Reg : public hwreg::RegisterBase<VdinPreOffset0_1Reg, uint32_t> {
 public:
  DEF_FIELD(28, 16, preoffset0);
  DEF_FIELD(12, 0, preoffset1);
  static auto Get() { return hwreg::RegisterAddr<VdinPreOffset0_1Reg>(VPU_VDIN1_PRE_OFFSET0_1); }
};

class VdinPreOffset2Reg : public hwreg::RegisterBase<VdinPreOffset2Reg, uint32_t> {
 public:
  DEF_FIELD(12, 0, preoffset2);
  static auto Get() { return hwreg::RegisterAddr<VdinPreOffset2Reg>(VPU_VDIN1_PRE_OFFSET2); }
};

class VdinLFifoCtrlReg : public hwreg::RegisterBase<VdinLFifoCtrlReg, uint32_t> {
 public:
  DEF_FIELD(11, 0, fifo_buf_size);
  static auto Get() { return hwreg::RegisterAddr<VdinLFifoCtrlReg>(VPU_VDIN1_LFIFO_CTRL); }
};

class VdinIntfWidthM1Reg : public hwreg::RegisterBase<VdinIntfWidthM1Reg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<VdinIntfWidthM1Reg>(VPU_VDIN1_INTF_WIDTHM1); }
};

class VdInWrCtrlReg : public hwreg::RegisterBase<VdInWrCtrlReg, uint32_t> {
 public:
  DEF_BIT(27, eol_sel);
  DEF_BIT(21, done_status_clear_bit);
  DEF_BIT(19, word_swap);
  DEF_FIELD(13, 12, memory_format);
  DEF_BIT(10, write_ctrl);
  DEF_BIT(9, write_req_urgent);
  DEF_BIT(8, write_mem_enable);
  DEF_FIELD(7, 0, canvas_idx);
  static auto Get() { return hwreg::RegisterAddr<VdInWrCtrlReg>(VPU_VDIN1_WR_CTRL); }
};

class VdInWrHStartEndReg : public hwreg::RegisterBase<VdInWrHStartEndReg, uint32_t> {
 public:
  DEF_BIT(29, reverse_enable);
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);
  static auto Get() { return hwreg::RegisterAddr<VdInWrHStartEndReg>(VPU_VDIN1_WR_H_START_END); }
};

class VdInWrVStartEndReg : public hwreg::RegisterBase<VdInWrVStartEndReg, uint32_t> {
 public:
  DEF_BIT(29, reverse_enable);
  DEF_FIELD(28, 16, start);
  DEF_FIELD(12, 0, end);
  static auto Get() { return hwreg::RegisterAddr<VdInWrVStartEndReg>(VPU_VDIN1_WR_V_START_END); }
};

class VdInAFifoCtrl3Reg : public hwreg::RegisterBase<VdInAFifoCtrl3Reg, uint32_t> {
 public:
  DEF_BIT(7, data_valid_en);
  DEF_BIT(6, go_field_en);
  DEF_BIT(5, go_line_en);
  DEF_BIT(4, vsync_pol_set);
  DEF_BIT(3, hsync_pol_set);
  DEF_BIT(2, vsync_sync_reset_en);
  DEF_BIT(1, fifo_overflow_clr);
  DEF_BIT(0, soft_reset_en);
  static auto Get() { return hwreg::RegisterAddr<VdInAFifoCtrl3Reg>(VPU_VDIN1_ASFIFO_CTRL3); }
};

class VdInMiscCtrlReg : public hwreg::RegisterBase<VdInMiscCtrlReg, uint32_t> {
 public:
  DEF_BIT(4, mif_reset);
  static auto Get() { return hwreg::RegisterAddr<VdInMiscCtrlReg>(VPU_VDIN1_MISC_CTRL); }
};

class VdInIfMuxCtrlReg : public hwreg::RegisterBase<VdInIfMuxCtrlReg, uint32_t> {
 public:
  DEF_FIELD(12, 8, vpu_path_1);  // bit defs are not documented.
  DEF_FIELD(4, 0, vpu_path_0);   // bit defs are not documented.
  static auto Get() { return hwreg::RegisterAddr<VdInIfMuxCtrlReg>(VPU_VIU_VDIN_IF_MUX_CTRL); }
};

}  // namespace astro_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_ASTRO_DISPLAY_VPU_REGS_H_
