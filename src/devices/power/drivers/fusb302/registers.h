// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_POWER_DRIVERS_FUSB302_REGISTERS_H_
#define SRC_DEVICES_POWER_DRIVERS_FUSB302_REGISTERS_H_

#include <hwreg/i2c.h>

#define DEVICE_ID_ADDR 0x01
#define SWITCHES0_ADDR 0x02
#define SWITCHES1_ADDR 0x03
#define MEASURE_ADDR 0x04
#define SLICE_ADDR 0x05
#define CONTROL0_ADDR 0x06
#define CONTROL1_ADDR 0x07
#define CONTROL2_ADDR 0x08
#define CONTROL3_ADDR 0x09
#define MASK_ADDR 0x0A
#define POWER_ADDR 0x0B
#define RESET_ADDR 0x0C
#define OCP_REG_ADDR 0x0D
#define MASK_A_ADDR 0x0E
#define MASK_B_ADDR 0x0F
#define CONTROL4_ADDR 0x10
#define STATUS0_A_ADDR 0x3C
#define STATUS1_A_ADDR 0x3D
#define INTERRUPT_A_ADDR 0x3E
#define INTERRUPT_B_ADDR 0x3F
#define STATUS0_ADDR 0x40
#define STATUS1_ADDR 0x41
#define INTERRUPT_ADDR 0x42
#define FIFOS_ADDR 0x43

constexpr float kVbusMeasureVoltageStep = 0.42f;
constexpr float kCcMeasureVoltageStep = 0.042f;

namespace fusb302 {

enum Polarity : bool {
  CC1 = false,
  CC2 = true,
};
enum PowerRole : bool {
  sink = false,
  source = true,
};
enum DataRole : uint8_t {
  DFP,
  UFP,
  DRP,
  ASS,
};

template <class RegType>
class Fusb302Reg : public hwreg::I2cRegisterBase<RegType, uint8_t, 1> {
 public:
  static RegType ReadFrom(const fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c) {
    auto reg = RegType::Get().FromValue(0);
    ZX_ASSERT(reg.I2cRegisterBase::ReadFrom(i2c) == ZX_OK);
    return reg;
  }
};

class DeviceIdReg : public Fusb302Reg<DeviceIdReg> {
 public:
  DEF_FIELD(7, 4, version_id);
  DEF_FIELD(3, 2, product_id);
  DEF_FIELD(1, 0, revision_id);

  static auto Get() { return hwreg::I2cRegisterAddr<DeviceIdReg>(DEVICE_ID_ADDR); }
};

class Switches0Reg : public Fusb302Reg<Switches0Reg> {
 public:
  DEF_BIT(7, pu_en2);
  DEF_BIT(6, pu_en1);
  DEF_BIT(5, vconn_cc2);
  DEF_BIT(4, vconn_cc1);
  DEF_BIT(3, meas_cc2);
  DEF_BIT(2, meas_cc1);
  DEF_BIT(1, pdwn2);
  DEF_BIT(0, pdwn1);

  static auto Get() { return hwreg::I2cRegisterAddr<Switches0Reg>(SWITCHES0_ADDR); }
};

class Switches1Reg : public Fusb302Reg<Switches1Reg> {
 public:
  DEF_BIT(7, power_role);
  DEF_FIELD(6, 5, spec_rev);
  DEF_BIT(4, data_role);
  DEF_BIT(2, auto_crc);
  DEF_BIT(1, txcc2);
  DEF_BIT(0, txcc1);

  static auto Get() { return hwreg::I2cRegisterAddr<Switches1Reg>(SWITCHES1_ADDR); }
};

class MeasureReg : public Fusb302Reg<MeasureReg> {
 public:
  DEF_BIT(6, meas_vbus);
  DEF_FIELD(5, 0, mdac);

  static auto Get() { return hwreg::I2cRegisterAddr<MeasureReg>(MEASURE_ADDR); }
};

class SliceReg : public Fusb302Reg<SliceReg> {
 public:
  DEF_FIELD(7, 6, sdac_hys);
  DEF_FIELD(5, 0, sdac);

  static auto Get() { return hwreg::I2cRegisterAddr<SliceReg>(SLICE_ADDR); }
};

class Control0Reg : public Fusb302Reg<Control0Reg> {
 public:
  DEF_BIT(6, tx_flush);
  DEF_BIT(5, int_mask);
  enum HostCur {
    NO_CURRENT = 0b00,
    DEFAULT = 0b01,
    MEDIUM_1A5 = 0b10,
    HIGH_3A0 = 0b11,
  };
  DEF_ENUM_FIELD(HostCur, 3, 2, host_cur);
  DEF_BIT(1, auto_pre);
  DEF_BIT(0, tx_start);

  static auto Get() { return hwreg::I2cRegisterAddr<Control0Reg>(CONTROL0_ADDR); }
};

class Control1Reg : public Fusb302Reg<Control1Reg> {
 public:
  DEF_BIT(6, ensop2db);
  DEF_BIT(5, ensop1db);
  DEF_BIT(4, bist_mode2);
  DEF_BIT(2, rx_flush);
  DEF_BIT(1, ensop2);
  DEF_BIT(0, ensop1);

  static auto Get() { return hwreg::I2cRegisterAddr<Control1Reg>(CONTROL1_ADDR); }
};

class Control2Reg : public Fusb302Reg<Control2Reg> {
 public:
  DEF_FIELD(7, 6, tog_save_pwr);
  DEF_BIT(5, tog_rd_only);
  DEF_BIT(3, wake_en);
  enum ToggleMode {
    ENABLE_DRP = 0b01,
    ENABLE_SNK = 0b10,
    ENABLE_SRC = 0b11,
  };
  DEF_ENUM_FIELD(ToggleMode, 2, 1, mode);
  DEF_BIT(0, toggle);

  static auto Get() { return hwreg::I2cRegisterAddr<Control2Reg>(CONTROL2_ADDR); }
};

class Control3Reg : public Fusb302Reg<Control3Reg> {
 public:
  DEF_BIT(6, send_hard_reset);
  DEF_BIT(5, bist_tmode);
  DEF_BIT(4, auto_hardreset);
  DEF_BIT(3, auto_softreset);
  DEF_FIELD(2, 1, n_retries);
  DEF_BIT(0, auto_retry);

  static auto Get() { return hwreg::I2cRegisterAddr<Control3Reg>(CONTROL3_ADDR); }
};

class MaskReg : public Fusb302Reg<MaskReg> {
 public:
  DEF_BIT(7, m_vbusok);
  DEF_BIT(6, m_activity);
  DEF_BIT(5, m_comp_chng);
  DEF_BIT(4, m_crc_chk);
  DEF_BIT(3, m_alert);
  DEF_BIT(2, m_wake);
  DEF_BIT(1, m_collision);
  DEF_BIT(0, m_bc_lvl);

  static auto Get() { return hwreg::I2cRegisterAddr<MaskReg>(MASK_ADDR); }
};

class PowerReg : public Fusb302Reg<PowerReg> {
 public:
  DEF_BIT(3, pwr3);
  DEF_BIT(2, pwr2);
  DEF_BIT(1, pwr1);
  DEF_BIT(0, pwr0);

  static auto Get() { return hwreg::I2cRegisterAddr<PowerReg>(POWER_ADDR); }
};

class ResetReg : public Fusb302Reg<ResetReg> {
 public:
  DEF_BIT(1, pd_reset);
  DEF_BIT(0, sw_res);

  static auto Get() { return hwreg::I2cRegisterAddr<ResetReg>(RESET_ADDR); }
};

class OcpReg : public Fusb302Reg<OcpReg> {
 public:
  DEF_BIT(3, ocp_range);
  DEF_FIELD(2, 0, ocp_cur);

  static auto Get() { return hwreg::I2cRegisterAddr<OcpReg>(OCP_REG_ADDR); }
};

class MaskAReg : public Fusb302Reg<MaskAReg> {
 public:
  DEF_BIT(7, m_ocp_temp);
  DEF_BIT(6, m_togdone);
  DEF_BIT(5, m_softfail);
  DEF_BIT(4, m_retryfail);
  DEF_BIT(3, m_hardsent);
  DEF_BIT(2, m_txsent);
  DEF_BIT(1, m_softrst);
  DEF_BIT(0, m_hardrst);

  static auto Get() { return hwreg::I2cRegisterAddr<MaskAReg>(MASK_A_ADDR); }
};

class MaskBReg : public Fusb302Reg<MaskBReg> {
 public:
  DEF_BIT(0, m_gcrcsent);

  static auto Get() { return hwreg::I2cRegisterAddr<MaskBReg>(MASK_B_ADDR); }
};

class Control4Reg : public Fusb302Reg<Control4Reg> {
 public:
  DEF_BIT(0, tog_exit_aud);

  static auto Get() { return hwreg::I2cRegisterAddr<Control4Reg>(CONTROL4_ADDR); }
};

class Status0AReg : public Fusb302Reg<Status0AReg> {
 public:
  DEF_BIT(5, softfail);
  DEF_BIT(4, retryfail);
  DEF_FIELD(3, 2, power);
  DEF_BIT(1, softrst);
  DEF_BIT(0, hardrst);

  static auto Get() { return hwreg::I2cRegisterAddr<Status0AReg>(STATUS0_A_ADDR); }
};

class Status1AReg : public Fusb302Reg<Status1AReg> {
 public:
  enum TogSS {
    toggle_running = 0b000,   // TOGGLE = 1
    stop_src1 = 0b001,        // SRCon CC1 (STOP_SRC1)
    stop_src2 = 0b010,        // SRCon CC2 (STOP_SRC2)
    stop_snk1 = 0b101,        // SNKon CC1 (STOP_SNK1)
    stop_snk2 = 0b110,        // SNKon CC2 (STOP_SNK2)
    audio_accessory = 0b111,  // AudioAccessory with vRa on both CC1 and CC2 (STOP_SRC1)
  };
  DEF_ENUM_FIELD(TogSS, 5, 3, togss);
  DEF_BIT(2, rxsop2db);
  DEF_BIT(1, rxsop1db);
  DEF_BIT(0, rxsop);

  static auto Get() { return hwreg::I2cRegisterAddr<Status1AReg>(STATUS1_A_ADDR); }
  static Polarity GetPolarity(TogSS val) { return (val & 0x01) ? CC1 : CC2; }
  static PowerRole GetPowerRole(TogSS val) { return (val & 0x04) ? sink : source; }
};

class InterruptAReg : public Fusb302Reg<InterruptAReg> {
 public:
  DEF_BIT(7, i_ocp_temp);
  DEF_BIT(6, i_togdone);
  DEF_BIT(5, i_softfail);
  DEF_BIT(4, i_retryfail);
  DEF_BIT(3, i_hardsent);
  DEF_BIT(2, i_txsent);
  DEF_BIT(1, i_softrst);
  DEF_BIT(0, i_hardrst);

  static auto Get() { return hwreg::I2cRegisterAddr<InterruptAReg>(INTERRUPT_A_ADDR); }
};

class InterruptBReg : public Fusb302Reg<InterruptBReg> {
 public:
  DEF_BIT(0, i_gcrcsent);

  static auto Get() { return hwreg::I2cRegisterAddr<InterruptBReg>(INTERRUPT_B_ADDR); }
};

class Status0Reg : public Fusb302Reg<Status0Reg> {
 public:
  DEF_BIT(7, vbusok);
  DEF_BIT(6, activity);
  DEF_BIT(5, comp);
  DEF_BIT(4, crc_chk);
  DEF_BIT(3, alert);
  DEF_BIT(2, wake);
  DEF_FIELD(1, 0, bc_lvl);

  static auto Get() { return hwreg::I2cRegisterAddr<Status0Reg>(STATUS0_ADDR); }
};

class Status1Reg : public Fusb302Reg<Status1Reg> {
 public:
  DEF_BIT(7, rxsop2);
  DEF_BIT(6, rxsop1);
  DEF_BIT(5, rx_empty);
  DEF_BIT(4, rx_full);
  DEF_BIT(3, tx_empty);
  DEF_BIT(2, tx_full);
  DEF_BIT(1, ovrtemp);
  DEF_BIT(0, ocp);

  static auto Get() { return hwreg::I2cRegisterAddr<Status1Reg>(STATUS1_ADDR); }
};

class InterruptReg : public Fusb302Reg<InterruptReg> {
 public:
  DEF_BIT(7, i_vbusok);
  DEF_BIT(6, i_activity);
  DEF_BIT(5, i_comp_chng);
  DEF_BIT(4, i_crc_chk);
  DEF_BIT(3, i_alert);
  DEF_BIT(2, i_wake);
  DEF_BIT(1, i_collision);
  DEF_BIT(0, i_bc_lvl);

  static auto Get() { return hwreg::I2cRegisterAddr<InterruptReg>(INTERRUPT_ADDR); }
};

class FifosReg : public Fusb302Reg<FifosReg> {
 public:
  DEF_FIELD(7, 0, tx_rx_token);

  static auto Get() { return hwreg::I2cRegisterAddr<FifosReg>(FIFOS_ADDR); }
};

}  // namespace fusb302

#endif  // SRC_DEVICES_POWER_DRIVERS_FUSB302_REGISTERS_H_
