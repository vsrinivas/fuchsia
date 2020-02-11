// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_SYSCONFIG_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_SYSCONFIG_H_

#include <hwreg/bitfields.h>
#include <hwreg/mmio.h>

//////////////////////////////////////////////////
// MIPI TX Registers
//////////////////////////////////////////////////
#define SYSCONFIG_DISP_OVL0_MOUT_EN (0x0030)
#define SYSCONFIG_DISP_DITHER_MOUT_EN (0x0038)
#define SYSCONFIG_DISP_UFOE_MOUT_EN (0x003C)
#define SYSCONFIG_DISP_COLOR0_SEL_IN (0x0058)
#define SYSCONFIG_DISP_UFOE_SEL_IN (0x0060)
#define SYSCONFIG_DSI0_SEL_IN (0x0064)
#define SYSCONFIG_DISP_RDMA0_SOUT_SEL_IN (0x006C)
#define SYSCONFIG_MMSYS_MISC (0x00F0)
#define SYSCONFIG_MMSYS_CG_CON0 (0x0100)
#define SYSCONFIG_MMSYS_CG_SET0 (0x0104)
#define SYSCONFIG_MMSYS_CG_CLR0 (0x0108)
#define SYSCONFIG_MMSYS_CG_CON1 (0x0110)
#define SYSCONFIG_MMSYS_CG_SET1 (0x0114)
#define SYSCONFIG_MMSYS_CG_CLR1 (0x0118)
#define SYSCONFIG_MMSYS_HW_DCM_DIS0 (0x0120)
#define SYSCONFIG_MMSYS_HW_DCM_DIS_SET0 (0x0124)
#define SYSCONFIG_MMSYS_HW_DCM_DIS_CLR0 (0x0128)
#define SYSCONFIG_MMSYS_SW0_RST_B (0x0140)
#define SYSCONFIG_MMSYS_SW1_RST_B (0x0144)
#define SYSCONFIG_MMSYS_LCM_RST_B (0x0150)
#define SYSCONFIG_MMSYS_DUMMY (0x0890)

namespace mt8167s_display {

class DispOvl0MoutEnReg : public hwreg::RegisterBase<DispOvl0MoutEnReg, uint32_t> {
 public:
  DEF_BIT(1, out_wdma);
  DEF_BIT(0, out_color);
  static auto Get() { return hwreg::RegisterAddr<DispOvl0MoutEnReg>(SYSCONFIG_DISP_OVL0_MOUT_EN); }
};

class DispDitherMoutEnReg : public hwreg::RegisterBase<DispDitherMoutEnReg, uint32_t> {
 public:
  DEF_BIT(2, out_wdma);
  DEF_BIT(1, out_ufoe);
  DEF_BIT(0, out_rdma0);
  static auto Get() {
    return hwreg::RegisterAddr<DispDitherMoutEnReg>(SYSCONFIG_DISP_DITHER_MOUT_EN);
  }
};

class DispUfoeMoutEnReg : public hwreg::RegisterBase<DispUfoeMoutEnReg, uint32_t> {
 public:
  DEF_BIT(2, out_wdma);
  DEF_BIT(1, out_dpi);
  DEF_BIT(0, out_dsi);
  static auto Get() { return hwreg::RegisterAddr<DispUfoeMoutEnReg>(SYSCONFIG_DISP_UFOE_MOUT_EN); }
};

class DispColor0SelInReg : public hwreg::RegisterBase<DispColor0SelInReg, uint32_t> {
 public:
  DEF_BIT(0, sel);
  static auto Get() {
    return hwreg::RegisterAddr<DispColor0SelInReg>(SYSCONFIG_DISP_COLOR0_SEL_IN);
  }
};

class DispUfoeSelInReg : public hwreg::RegisterBase<DispUfoeSelInReg, uint32_t> {
 public:
  DEF_BIT(0, sel);
  static auto Get() { return hwreg::RegisterAddr<DispUfoeSelInReg>(SYSCONFIG_DISP_UFOE_SEL_IN); }
};

class Dsi0SelInReg : public hwreg::RegisterBase<Dsi0SelInReg, uint32_t> {
 public:
  DEF_FIELD(1, 0, sel);
  static auto Get() { return hwreg::RegisterAddr<Dsi0SelInReg>(SYSCONFIG_DSI0_SEL_IN); }
};

class DispRdma0SoutSelInReg : public hwreg::RegisterBase<DispRdma0SoutSelInReg, uint32_t> {
 public:
  DEF_FIELD(1, 0, sel);
  static auto Get() {
    return hwreg::RegisterAddr<DispRdma0SoutSelInReg>(SYSCONFIG_DISP_RDMA0_SOUT_SEL_IN);
  }
};

class MmsysCgCon0Reg : public hwreg::RegisterBase<MmsysCgCon0Reg, uint32_t> {
 public:
  DEF_BIT(19, ufoe);
  DEF_BIT(18, dither);
  DEF_BIT(17, gamma);
  DEF_BIT(16, aal);
  DEF_BIT(15, ccorr);
  DEF_BIT(14, color0);
  DEF_BIT(13, wdma0);
  DEF_BIT(12, rdma1);
  DEF_BIT(11, rdma0);
  DEF_BIT(10, ovl0);
  DEF_BIT(1, smi_larb0);
  DEF_BIT(0, smi_common);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgCon0Reg>(SYSCONFIG_MMSYS_CG_CON0); }
};

class MmsysCgSet0Reg : public hwreg::RegisterBase<MmsysCgSet0Reg, uint32_t> {
 public:
  DEF_BIT(19, ufoe);
  DEF_BIT(18, dither);
  DEF_BIT(17, gamma);
  DEF_BIT(16, aal);
  DEF_BIT(15, ccorr);
  DEF_BIT(14, color0);
  DEF_BIT(13, wdma0);
  DEF_BIT(12, rdma1);
  DEF_BIT(11, rdma0);
  DEF_BIT(10, ovl0);
  DEF_BIT(1, smi_larb0);
  DEF_BIT(0, smi_common);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgSet0Reg>(SYSCONFIG_MMSYS_CG_SET0); }
};

class MmsysCgClr0Reg : public hwreg::RegisterBase<MmsysCgClr0Reg, uint32_t> {
 public:
  DEF_BIT(19, ufoe);
  DEF_BIT(18, dither);
  DEF_BIT(17, gamma);
  DEF_BIT(16, aal);
  DEF_BIT(15, ccorr);
  DEF_BIT(14, color0);
  DEF_BIT(13, wdma0);
  DEF_BIT(12, rdma1);
  DEF_BIT(11, rdma0);
  DEF_BIT(10, ovl0);
  DEF_BIT(1, smi_larb0);
  DEF_BIT(0, smi_common);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgClr0Reg>(SYSCONFIG_MMSYS_CG_CLR0); }
};

class MmsysCgCon1Reg : public hwreg::RegisterBase<MmsysCgCon1Reg, uint32_t> {
 public:
  DEF_BIT(17, dpi1_eng);
  DEF_BIT(16, dpi1_pix);
  DEF_BIT(15, lvds_cts);
  DEF_BIT(14, lvds_pix);
  DEF_BIT(5, dpi0_eng);
  DEF_BIT(4, dpi0_pix);
  DEF_BIT(3, dsi0_dig);
  DEF_BIT(2, dsi0_eng);
  DEF_BIT(1, pwm0_26m);
  DEF_BIT(0, pwm0_mm);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgCon1Reg>(SYSCONFIG_MMSYS_CG_CON1); }
};

class MmsysCgSet1Reg : public hwreg::RegisterBase<MmsysCgSet1Reg, uint32_t> {
 public:
  DEF_BIT(17, dpi1_eng);
  DEF_BIT(16, dpi1_pix);
  DEF_BIT(15, lvds_cts);
  DEF_BIT(14, lvds_pix);
  DEF_BIT(5, dpi0_eng);
  DEF_BIT(4, dpi0_pix);
  DEF_BIT(3, dsi0_dig);
  DEF_BIT(2, dsi0_eng);
  DEF_BIT(1, pwm0_26m);
  DEF_BIT(0, pwm0_mm);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgSet1Reg>(SYSCONFIG_MMSYS_CG_SET1); }
};

class MmsysCgClr1Reg : public hwreg::RegisterBase<MmsysCgClr1Reg, uint32_t> {
 public:
  DEF_BIT(17, dpi1_eng);
  DEF_BIT(16, dpi1_pix);
  DEF_BIT(15, lvds_cts);
  DEF_BIT(14, lvds_pix);
  DEF_BIT(5, dpi0_eng);
  DEF_BIT(4, dpi0_pix);
  DEF_BIT(3, dsi0_dig);
  DEF_BIT(2, dsi0_eng);
  DEF_BIT(1, pwm0_26m);
  DEF_BIT(0, pwm0_mm);
  static auto Get() { return hwreg::RegisterAddr<MmsysCgClr1Reg>(SYSCONFIG_MMSYS_CG_CLR1); }
};

class MmsysLcmRstReg : public hwreg::RegisterBase<MmsysLcmRstReg, uint32_t> {
 public:
  DEF_BIT(0, reset);
  static auto Get() { return hwreg::RegisterAddr<MmsysLcmRstReg>(SYSCONFIG_MMSYS_LCM_RST_B); }
};

class MmsysDummyReg : public hwreg::RegisterBase<MmsysDummyReg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<MmsysDummyReg>(SYSCONFIG_MMSYS_DUMMY); }
};

}  // namespace mt8167s_display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_MT8167S_DISPLAY_REGISTERS_SYSCONFIG_H_
