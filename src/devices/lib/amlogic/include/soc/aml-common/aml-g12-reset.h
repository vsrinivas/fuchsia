// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_G12_RESET_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_G12_RESET_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

// Reset registers common to g12a and g12b

namespace aml_reset {

class RESET_0 : public hwreg::RegisterBase<RESET_0, uint32_t> {
 public:
  DEF_BIT(26, vcbus);
  DEF_BIT(25, ahb_data);
  DEF_BIT(24, ahb_cntl);
  DEF_BIT(23, cbus_capb3);
  DEF_BIT(21, dos_capb3);
  DEF_BIT(20, dvalin_capb3);
  DEF_BIT(19, hdmitx_capb3);
  DEF_BIT(17, decode_capb3);
  DEF_BIT(15, pcie_apb);
  DEF_BIT(14, pcie_phy);
  DEF_BIT(13, vcbus_2);
  DEF_BIT(12, pcie_ctrl_a);
  DEF_BIT(11, assist);
  DEF_BIT(10, venc);
  DEF_BIT(7, vid_pll_div);
  DEF_BIT(6, afifo);
  DEF_BIT(5, viu);
  DEF_BIT(3, ddr);
  DEF_BIT(2, dos);
  DEF_BIT(0, hiu);
  static auto Get() { return hwreg::RegisterAddr<RESET_0>(0x4); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_0>(0x40); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_0>(0x80); }
};

class RESET_1 : public hwreg::RegisterBase<RESET_1, uint32_t> {
 public:
  DEF_BIT(29, audio_codec);
  // TODO: See fxb/43747
  DEF_BIT(17, unknown_field_b);
  DEF_BIT(16, unknown_field_a);
  DEF_BIT(14, sd_emmc_c);
  DEF_BIT(13, sd_emmc_b);
  DEF_BIT(12, sd_emmc_a);
  DEF_BIT(11, eth);
  DEF_BIT(10, isa);
  DEF_BIT(8, parser);
  DEF_BIT(6, ahb_sram);
  DEF_BIT(5, bt656);
  DEF_BIT(3, ddr);
  DEF_BIT(2, usb);
  DEF_BIT(1, demux);
  static auto Get() { return hwreg::RegisterAddr<RESET_1>(0x8); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_1>(0x44); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_1>(0x84); }
};

class RESET_2 : public hwreg::RegisterBase<RESET_2, uint32_t> {
 public:
  DEF_BIT(15, hdmitx);
  DEF_BIT(14, dvalin);
  DEF_BIT(10, parser_top);
  DEF_BIT(9, parser_ctl);
  DEF_BIT(8, parser_fetch);
  DEF_BIT(7, parser_reg);
  DEF_BIT(6, ge2d);
  DEF_BIT(5, alocker);
  DEF_BIT(4, mipi_dsi_host);
  DEF_BIT(2, hdmi_tx);
  DEF_BIT(1, audio);
  static auto Get() { return hwreg::RegisterAddr<RESET_2>(0xc); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_2>(0x48); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_2>(0x88); }
};

class RESET_3 : public hwreg::RegisterBase<RESET_3, uint32_t> {
 public:
  DEF_BIT(15, demux_2);
  DEF_BIT(14, demux_1);
  DEF_BIT(13, demux_0);
  DEF_BIT(12, demux_s2p_1);
  DEF_BIT(11, demux_s2p_0);
  DEF_BIT(10, demux_des_pl);
  DEF_BIT(9, demux_top);
  static auto Get() { return hwreg::RegisterAddr<RESET_3>(0x10); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_3>(0x4c); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_3>(0x8c); }
};

class RESET_4 : public hwreg::RegisterBase<RESET_4, uint32_t> {
 public:
  DEF_BIT(15, i2c_m2);
  DEF_BIT(14, i2c_m1);
  DEF_BIT(13, vencl);
  DEF_BIT(12, vdi6);
  DEF_BIT(9, vdac);
  DEF_BIT(7, vencp);
  DEF_BIT(6, venci);
  DEF_BIT(5, rdma);
  DEF_BIT(2, mipi_dsiphy);
  static auto Get() { return hwreg::RegisterAddr<RESET_4>(0x14); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_4>(0x50); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_4>(0x90); }
};

class RESET_6 : public hwreg::RegisterBase<RESET_6, uint32_t> {
 public:
  DEF_BIT(14, i2c_m3);
  DEF_BIT(13, spifc0);
  DEF_BIT(12, async1);
  DEF_BIT(11, async0);
  DEF_BIT(10, uart1_2);
  DEF_BIT(9, uart_ee_a);
  DEF_BIT(8, ts_cpu);
  DEF_BIT(7, stream);
  DEF_BIT(6, spicc1);
  DEF_BIT(5, ts_pll);
  DEF_BIT(4, i2c_m0);
  DEF_BIT(3, sana_3);
  DEF_BIT(2, sc);
  DEF_BIT(1, spicc0);
  DEF_BIT(0, gen);
  static auto Get() { return hwreg::RegisterAddr<RESET_6>(0x1c); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_6>(0x58); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_6>(0x98); }
};

class RESET_7 : public hwreg::RegisterBase<RESET_7, uint32_t> {
 public:
  DEF_BIT(13, hevcf_dmc_pipl);
  DEF_BIT(12, wave420_dmc_pipl);
  DEF_BIT(11, hcodec_dmc_pipl);
  DEF_BIT(10, ge2d_dmc_pipl);
  DEF_BIT(9, dmc_vpu_pipl);
  DEF_BIT(8, nic_dmc_pipl);
  DEF_BIT(7, vid_lock);
  DEF_BIT(6, dvalin_dmc_pipl);
  DEF_BIT(5, device_mmc_arb);
  DEF_BIT(4, ts_gpu);
  DEF_BIT(3, usb_ddr3);
  DEF_BIT(2, usb_ddr2);
  DEF_BIT(1, usb_ddr1);
  DEF_BIT(0, usb_ddr0);
  static auto Get() { return hwreg::RegisterAddr<RESET_7>(0x20); }
  static auto GetMask() { return hwreg::RegisterAddr<RESET_7>(0x5c); }
  static auto GetLevel() { return hwreg::RegisterAddr<RESET_7>(0x9c); }
};

}  // namespace aml_reset

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_G12_RESET_H_
