// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_USB_PHY_REGS_H_
#define SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_USB_PHY_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace aml_usb_phy {

// PHY register offsets
constexpr uint32_t U2P_REGISTER_OFFSET = 32;
constexpr uint32_t U2P_R0_OFFSET = 0x0;
constexpr uint32_t U2P_R1_OFFSET = 0x4;

constexpr uint32_t USB_R0_OFFSET = 0x80;
constexpr uint32_t USB_R1_OFFSET = 0x84;
constexpr uint32_t USB_R2_OFFSET = 0x88;
constexpr uint32_t USB_R3_OFFSET = 0x8c;
constexpr uint32_t USB_R4_OFFSET = 0x90;
constexpr uint32_t USB_R5_OFFSET = 0x94;

class U2P_R0_V2 : public hwreg::RegisterBase<U2P_R0_V2, uint32_t> {
 public:
  DEF_BIT(0, host_device);
  DEF_BIT(1, power_ok);
  DEF_BIT(2, hast_mode);
  DEF_BIT(3, por);
  DEF_BIT(4, idpullup0);
  DEF_BIT(5, drvvbus0);
  static auto Get(uint32_t i) {
    return hwreg::RegisterAddr<U2P_R0_V2>(i * U2P_REGISTER_OFFSET + U2P_R0_OFFSET);
  }
};

class U2P_R1_V2 : public hwreg::RegisterBase<U2P_R1_V2, uint32_t> {
 public:
  DEF_BIT(0, phy_rdy);
  DEF_BIT(1, iddig0);
  DEF_BIT(2, otgsessvld0);
  DEF_BIT(3, vbusvalid0);
  static auto Get(uint32_t i) {
    return hwreg::RegisterAddr<U2P_R1_V2>(i * U2P_REGISTER_OFFSET + U2P_R1_OFFSET);
  }
};

class USB_R0_V2 : public hwreg::RegisterBase<USB_R0_V2, uint32_t> {
 public:
  DEF_BIT(17, p30_lane0_tx2rx_loopback);
  DEF_BIT(18, p30_lane0_ext_pclk_reg);
  DEF_FIELD(28, 19, p30_pcs_rx_los_mask_val);
  DEF_FIELD(30, 29, u2d_ss_scaledown_mode);
  DEF_BIT(31, u2d_act);
  static auto Get() { return hwreg::RegisterAddr<USB_R0_V2>(USB_R0_OFFSET); }
};

class USB_R1_V2 : public hwreg::RegisterBase<USB_R1_V2, uint32_t> {
 public:
  DEF_BIT(0, u3h_bigendian_gs);
  DEF_BIT(1, u3h_pme_en);
  DEF_FIELD(3, 2, u3h_hub_port_overcurrent);
  DEF_FIELD(8, 6, u3h_hub_port_perm_attach);
  DEF_FIELD(12, 11, u3h_host_u2_port_disable);
  DEF_BIT(16, u3h_host_u3_port_disable);
  DEF_BIT(17, u3h_host_port_power_control_present);
  DEF_BIT(18, u3h_host_msi_enable);
  DEF_FIELD(24, 19, u3h_fladj_30mhz_reg);
  DEF_FIELD(31, 25, p30_pcs_tx_swing_full);
  static auto Get() { return hwreg::RegisterAddr<USB_R1_V2>(USB_R1_OFFSET); }
};

class USB_R2_V2 : public hwreg::RegisterBase<USB_R2_V2, uint32_t> {
 public:
  DEF_FIELD(25, 20, p30_pcs_tx_deemph_3p5db);
  DEF_FIELD(31, 26, p30_pcs_tx_deemph_6db);
  static auto Get() { return hwreg::RegisterAddr<USB_R2_V2>(USB_R2_OFFSET); }
};

class USB_R3_V2 : public hwreg::RegisterBase<USB_R3_V2, uint32_t> {
 public:
  DEF_BIT(0, p30_ssc_en);
  DEF_FIELD(3, 1, p30_ssc_range);
  DEF_FIELD(12, 4, p30_ssc_ref_clk_sel);
  DEF_BIT(13, p30_ref_ssp_en);
  static auto Get() { return hwreg::RegisterAddr<USB_R3_V2>(USB_R3_OFFSET); }
};

class USB_R4_V2 : public hwreg::RegisterBase<USB_R4_V2, uint32_t> {
 public:
  DEF_BIT(0, p21_portreset0);
  DEF_BIT(1, p21_sleepm0);
  DEF_FIELD(3, 2, mem_pd);
  DEF_BIT(4, p21_only);
  static auto Get() { return hwreg::RegisterAddr<USB_R4_V2>(USB_R4_OFFSET); }
};

class USB_R5_V2 : public hwreg::RegisterBase<USB_R5_V2, uint32_t> {
 public:
  DEF_BIT(0, iddig_sync);
  DEF_BIT(1, iddig_reg);
  DEF_FIELD(3, 2, iddig_cfg);
  DEF_BIT(4, iddig_en0);
  DEF_BIT(5, iddig_en1);
  DEF_BIT(6, iddig_curr);
  DEF_BIT(7, usb_iddig_irq);
  DEF_FIELD(15, 8, iddig_th);
  DEF_FIELD(23, 16, iddig_cnt);
  static auto Get() { return hwreg::RegisterAddr<USB_R5_V2>(USB_R5_OFFSET); }
};

// Undocumented PLL registers used for PHY tuning.
class PLL_REGISTER : public hwreg::RegisterBase<PLL_REGISTER, uint32_t> {
 public:
  static auto Get(uint32_t i) { return hwreg::RegisterAddr<PLL_REGISTER>(i); }
};

class PLL_REGISTER_40 : public hwreg::RegisterBase<PLL_REGISTER_40, uint32_t> {
 public:
  DEF_FIELD(27, 0, value);
  DEF_BIT(28, enable);
  DEF_BIT(29, reset);
  static auto Get() { return hwreg::RegisterAddr<PLL_REGISTER_40>(0x40); }
};

}  // namespace aml_usb_phy

#endif  // SRC_DEVICES_USB_DRIVERS_AML_USB_PHY_V2_USB_PHY_REGS_H_
