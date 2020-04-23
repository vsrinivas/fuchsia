// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_PHY_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_PHY_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace board_mt8167 {

// USB20 PHYA Common 0 Register (controller-0)
class USBPHYACR0 : public hwreg::RegisterBase<USBPHYACR0, uint32_t> {
 public:
  DEF_FIELD(30, 28, mpx_out_sel);
  DEF_FIELD(26, 24, tx_ph_rot_sel);
  DEF_FIELD(22, 20, pll_diven);
  DEF_BIT(18, pll_br);
  DEF_BIT(17, pll_bp);
  DEF_BIT(16, pll_blp);
  DEF_BIT(15, usbpll_force_on);
  DEF_FIELD(14, 8, pll_fbdiv);
  DEF_FIELD(7, 6, pll_prediv);
  DEF_BIT(5, intr_en);
  DEF_BIT(4, ref_en);
  DEF_FIELD(3, 2, bgr_div);
  DEF_BIT(1, chp_en);
  DEF_BIT(0, bgr_en);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR0>(0x00); }
};

// USB20 PHYA Common 1 Register (controller-0)
class USBPHYACR1 : public hwreg::RegisterBase<USBPHYACR1, uint32_t> {
 public:
 public:
  DEF_FIELD(31, 24, clkref_rev_7_0);
  DEF_FIELD(23, 19, intr_cal);
  DEF_FIELD(18, 16, otg_vbusth);
  DEF_FIELD(14, 12, vrt_vref_sel);
  DEF_FIELD(10, 8, term_vref_sel);
  DEF_FIELD(7, 0, mpx_sel);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR1>(0x04); }
};

// USB20 PHYA Common 2 Register (controller-0)
class USBPHYACR2 : public hwreg::RegisterBase<USBPHYACR2, uint32_t> {
 public:
 public:
  DEF_FIELD(7, 0, clkref_rev_18_8);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR2>(0x08); }
};

// USB20 PHYA Common 4 Register (controller-0)
class USBPHYACR4 : public hwreg::RegisterBase<USBPHYACR4, uint32_t> {
 public:
 public:
  DEF_BIT(31, dp_abist_source_en);
  DEF_FIELD(27, 24, dp_abist_sele);
  DEF_BIT(16, icusb_en);
  DEF_FIELD(14, 12, ls_cr);
  DEF_FIELD(10, 8, fs_cr);
  DEF_FIELD(6, 4, ls_sr);
  DEF_FIELD(2, 0, fs_sr);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR4>(0x10); }
};

// USB20 PHYA Common 5 Register (controller-0)
class USBPHYACR5 : public hwreg::RegisterBase<USBPHYACR5, uint32_t> {
 public:
 public:
  DEF_BIT(28, disc_fit_en);
  DEF_FIELD(27, 26, init_sq_en_dg);
  DEF_FIELD(25, 24, hstx_tmode_sel);
  DEF_FIELD(23, 22, sqd);
  DEF_FIELD(21, 20, discd);
  DEF_BIT(19, hstx_tmode_en);
  DEF_BIT(18, phyd_monen);
  DEF_BIT(17, inlpbk_en);
  DEF_BIT(16, chirp_en);
  DEF_BIT(15, hstx_srcal_en);
  DEF_FIELD(14, 12, hstx_srctrl);
  DEF_BIT(11, hs_100u_u3_en);
  DEF_BIT(10, gbias_enb);
  DEF_BIT(7, dm_abist_source_en);
  DEF_FIELD(3, 0, dm_abist_sele);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR5>(0x14); }
};

// USB20 PHYA Common 6 Register
class USBPHYACR6 : public hwreg::RegisterBase<USBPHYACR6, uint32_t> {
 public:
 public:
  DEF_FIELD(31, 24, phy_rev_7_0);
  DEF_BIT(23, bc11_sw_en);
  DEF_BIT(22, sr_clk_sel);
  DEF_BIT(20, otg_vbuscmp_en);
  DEF_BIT(19, otg_abist_en);
  DEF_FIELD(18, 16, otg_abist_sele);
  DEF_FIELD(13, 12, hsrx_mmode_sele);
  DEF_FIELD(10, 9, hsrx_bias_en_sel);
  DEF_BIT(8, hsrx_tmode_en);
  DEF_FIELD(7, 4, discth);
  DEF_FIELD(3, 0, sqth);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR6>(0x18); }
};

// USB20 PHYA Control 3 Register (controller-0)
class U2PHYACR3 : public hwreg::RegisterBase<U2PHYACR3, uint32_t> {
 public:
 public:
  DEF_FIELD(31, 28, hstx_dbist);
  DEF_BIT(26, hstx_bist_en);
  DEF_FIELD(25, 24, hstx_i_en_mode);
  DEF_BIT(19, usb11_tmode_en);
  DEF_BIT(18, tmode_fs_ls_tx_en);
  DEF_BIT(17, tmode_fx_ls_rcv_en);
  DEF_BIT(16, tmode_fs_ls_mode);
  DEF_FIELD(14, 13, hs_term_en_mode);
  DEF_BIT(12, pupd_bist_en);
  DEF_BIT(11, en_pu_dm);
  DEF_BIT(10, en_pd_dm);
  DEF_BIT(9, en_pu_dp);
  DEF_BIT(8, en_pd_dp);
  static auto Get() { return hwreg::RegisterAddr<U2PHYACR3>(0x1c); }
};

// USB20 PHYA Control 4 Register (controller-0)
class U2PHYACR4 : public hwreg::RegisterBase<U2PHYACR4, uint32_t> {
 public:
 public:
  DEF_BIT(18, dp_100k_mode);
  DEF_BIT(17, dm_100k_en);
  DEF_BIT(16, dp_100k_en);
  DEF_BIT(15, usb20_gpio_dm_i);
  DEF_BIT(14, usb20_gpio_dp_i);
  DEF_BIT(13, usb20_gpio_dm_oe);
  DEF_BIT(12, usb20_gpio_dp_oe);
  DEF_BIT(9, usb20_gpio_ctl);
  DEF_BIT(8, usb20_gpio_mode);
  DEF_BIT(5, tx_bias_en);
  DEF_BIT(4, tx_vcmpdn_en);
  DEF_FIELD(3, 2, hs_sq_en_mode);
  DEF_FIELD(1, 0, hs_rcv_en_mode);
  static auto Get() { return hwreg::RegisterAddr<U2PHYACR4>(0x20); }
};

// USB20 PHYD Control UTMI 0 Register (controller-0)
class U2PHYDTM0 : public hwreg::RegisterBase<U2PHYDTM0, uint32_t> {
 public:
 public:
  DEF_FIELD(31, 30, rg_uart_mode);
  DEF_BIT(29, force_uart_i);
  DEF_BIT(28, force_uart_bias_en);
  DEF_BIT(27, force_uart_tx_oe);
  DEF_BIT(26, force_uart_en);
  DEF_BIT(25, force_usb_clken);
  DEF_BIT(24, force_drvvbus);
  DEF_BIT(23, force_datain);
  DEF_BIT(22, force_txvalid);
  DEF_BIT(21, force_dm_pulldown);
  DEF_BIT(20, force_dp_pulldown);
  DEF_BIT(19, force_xcvsel);
  DEF_BIT(18, force_suspendm);
  DEF_BIT(17, force_termsel);
  DEF_BIT(16, force_opmode);
  DEF_BIT(15, utmi_muxsel);
  DEF_BIT(14, rg_reset);
  DEF_FIELD(13, 10, rg_datain);
  DEF_BIT(9, rg_txvalidh);
  DEF_BIT(8, rg_txvalid);
  DEF_BIT(7, rg_dmpulldown);
  DEF_BIT(6, rg_dppulldown);
  DEF_FIELD(5, 4, rg_xcvrsel);
  DEF_BIT(3, rg_suspendm);
  DEF_BIT(2, rg_termsel);
  DEF_FIELD(1, 0, rg_opmode);
  static auto Get() { return hwreg::RegisterAddr<U2PHYDTM0>(0x68); }
};

// USB20 PHYD Control UTMI 1 Register (controller-0)
class U2PHYDTM1 : public hwreg::RegisterBase<U2PHYDTM1, uint32_t> {
 public:
 public:
  DEF_BIT(31, prbs7_en);
  DEF_FIELD(29, 24, prbs7_bitcnt);
  DEF_BIT(23, clk48m_en);
  DEF_BIT(22, clk60m_en);
  DEF_BIT(19, rg_uart_i);
  DEF_BIT(18, rg_uart_bias_en);
  DEF_BIT(17, rg_uart_tx_oe);
  DEF_BIT(16, rg_uart_en);
  DEF_BIT(13, force_vbusvalid);
  DEF_BIT(12, force_sessend);
  DEF_BIT(11, force_bvalid);
  DEF_BIT(10, force_avalid);
  DEF_BIT(9, force_iddig);
  DEF_BIT(8, force_idpullup);
  DEF_BIT(5, rg_vbusvalid);
  DEF_BIT(4, rg_sessend);
  DEF_BIT(3, rg_bvalid);
  DEF_BIT(2, rg_avalid);
  DEF_BIT(1, rg_iddig);
  DEF_BIT(0, rg_idpullup);
  static auto Get() { return hwreg::RegisterAddr<U2PHYDTM1>(0x6c); }
};

// USB20 PHYA Common 6 Register (controller-1)
class USBPHYACR6_1P : public hwreg::RegisterBase<USBPHYACR6_1P, uint32_t> {
 public:
  DEF_FIELD(31, 24, rg_usb20_phy_rev);
  DEF_BIT(23, rg_usb20_bc11_sw_en);
  DEF_BIT(22, rg_usb20_sr_clk_sel);
  DEF_BIT(20, rg_usb20_otg_vbuscmp_en);
  DEF_BIT(19, rg_usb20_otg_abist_en);
  DEF_FIELD(18, 16, rg_usb20_otg_abist_sele);
  DEF_FIELD(13, 12, rg_usb20_hsrx_mmode_sele);
  DEF_FIELD(10, 9, rg_usb20_hsrx_bias_en_sel);
  DEF_BIT(8, rg_usb20_hsrx_tmode_en);
  DEF_FIELD(7, 4, rg_usb20_discth);
  DEF_FIELD(3, 0, rg_usb20_sqth);
  static auto Get() { return hwreg::RegisterAddr<USBPHYACR6_1P>(0x118); }
};

class U2PHYACR3_1P : public hwreg::RegisterBase<U2PHYACR3_1P, uint32_t> {
 public:
  DEF_FIELD(31, 28, rg_usb20_hstx_dbist);
  DEF_BIT(26, rg_usb20_hstx_bist_en);
  DEF_FIELD(25, 24, rg_usb20_hstx_i_en_mode);
  DEF_BIT(19, rg_usb20_usb11_tmode_en);
  DEF_BIT(18, rg_usb20_tmode_fs_ls_tx_en);
  DEF_BIT(17, rg_usb20_tmode_fs_ls_rcv_en);
  DEF_BIT(16, rg_usb20_tmode_fs_ls_mode);
  DEF_FIELD(14, 13, rg_usb20_hs_term_en_mode);
  DEF_BIT(12, rg_usb20_pupd_bist_en);
  DEF_BIT(11, rg_usb20_en_pu_dm);
  DEF_BIT(10, rg_usb20_en_pd_dm);
  DEF_BIT(9, rg_usb20_en_pu_dp);
  DEF_BIT(8, rg_usb20_en_pd_dp);
  static auto Get() { return hwreg::RegisterAddr<U2PHYACR3_1P>(0x11c); }
};

// USB20 PHYA Control 4 Register (controller-1)
class U2PHYACR4_1P : public hwreg::RegisterBase<U2PHYACR4_1P, uint32_t> {
 public:
  DEF_BIT(18, rg_usb20_dp_100k_mode);
  DEF_BIT(17, rg_usb20_dm_100k_en);
  DEF_BIT(16, usb20_dp_100k_en);
  DEF_BIT(15, usb20_gpio_dm_i);
  DEF_BIT(14, usb20_gpio_dp_i);
  DEF_BIT(13, usb20_gpio_dm_oe);
  DEF_BIT(12, usb20_gpio_dp_oe);
  DEF_BIT(9, rg_usb20_gpio_ctl);
  DEF_BIT(8, usb20_gpio_mode);
  DEF_BIT(5, rg_usb20_tx_bias_en);
  DEF_BIT(4, rg_usb20_tx_vcmpdn_en);
  DEF_FIELD(3, 2, rg_usb20_hs_sq_en_mode);
  DEF_FIELD(1, 0, rg_usb20_hs_rcv_en_mode);
  static auto Get() { return hwreg::RegisterAddr<U2PHYACR4_1P>(0x120); }
};

// USB20 PHYD Control UTMI 0 Register (controller-1)
class U2PHYDTM0_1P : public hwreg::RegisterBase<U2PHYDTM0_1P, uint32_t> {
 public:
  DEF_FIELD(31, 30, rg_uart_mode);
  DEF_BIT(29, force_uart_i);
  DEF_BIT(28, force_uart_bias_en);
  DEF_BIT(27, force_uart_tx_oe);
  DEF_BIT(26, force_uart_en);
  DEF_BIT(25, force_usb_clken);
  DEF_BIT(24, force_drvbus);
  DEF_BIT(23, force_datain);
  DEF_BIT(22, force_txvalid);
  DEF_BIT(21, force_dm_pulldown);
  DEF_BIT(20, force_dp_pulldown);
  DEF_BIT(19, force_xcvrsel);
  DEF_BIT(18, force_suspendm);
  DEF_BIT(17, force_termsel);
  DEF_BIT(16, force_opmode);
  DEF_BIT(15, utmi_muxsel);
  DEF_BIT(14, rg_reset);
  DEF_FIELD(13, 10, rg_datain);
  DEF_BIT(9, rg_txvalidh);
  DEF_BIT(8, rg_txvalid);
  DEF_BIT(7, rg_dmpulldown);
  DEF_BIT(6, rg_dppulldown);
  DEF_FIELD(5, 4, rg_xcvrsel);
  DEF_BIT(3, rg_suspendm);
  DEF_BIT(2, rg_termsel);
  DEF_FIELD(1, 0, rg_opmode);
  static auto Get() { return hwreg::RegisterAddr<U2PHYDTM0_1P>(0x168); }
};

// USB20 PHYD Control UTMI 1 Register (controller-1)
class U2PHYDTM1_1P : public hwreg::RegisterBase<U2PHYDTM1_1P, uint32_t> {
 public:
  DEF_BIT(31, rg_usb20_prbs7_en);
  DEF_FIELD(29, 24, rg_usb20_prbs7_bitcnt);
  DEF_BIT(23, rg_usb20_clk48m_en);
  DEF_BIT(22, rg_usb20_clk60m_en);
  DEF_BIT(19, rg_uart_i);
  DEF_BIT(18, rg_uart_bias_en);
  DEF_BIT(17, rg_uart_tx_oe);
  DEF_BIT(16, rg_uart_en);
  DEF_BIT(13, force_vbusvalid);
  DEF_BIT(12, force_sessend);
  DEF_BIT(11, force_bvalid);
  DEF_BIT(10, force_avalid);
  DEF_BIT(9, force_iddig);
  DEF_BIT(8, force_idpullup);
  DEF_BIT(5, rg_vbusvalid);
  DEF_BIT(4, rg_sessend);
  DEF_BIT(3, rg_bvalid);
  DEF_BIT(2, rg_avalid);
  DEF_BIT(1, rg_iddig);
  DEF_BIT(0, rg_idpullup);
  static auto Get() { return hwreg::RegisterAddr<U2PHYDTM1_1P>(0x16c); }
};

}  // namespace board_mt8167

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_PHY_H_
