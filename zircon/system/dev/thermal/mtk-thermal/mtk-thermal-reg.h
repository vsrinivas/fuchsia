// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <lib/mmio/mmio.h>

namespace thermal {

class ArmPllCon1 : public hwreg::RegisterBase<ArmPllCon1, uint32_t> {
 public:
  static constexpr uint32_t kDiv1 = 0;
  static constexpr uint32_t kDiv2 = 1;
  static constexpr uint32_t kDiv4 = 2;
  static constexpr uint32_t kDiv8 = 3;
  static constexpr uint32_t kDiv16 = 4;

  static constexpr uint32_t kPcwFracBits = 14;

  static constexpr uint32_t kPllSrcClk = 26000000;

  static auto Get() { return hwreg::RegisterAddr<ArmPllCon1>(0x104); }

  uint32_t frequency() const {
    uint64_t freq = static_cast<uint64_t>(pcw()) * kPllSrcClk;
    return static_cast<uint32_t>(freq >> (kPcwFracBits + div()));
  }

  ArmPllCon1& set_frequency(uint32_t freq_hz) {
    set_change(1);
    set_div(kDiv1);
    uint64_t pcw_value = static_cast<uint64_t>(freq_hz) << (kPcwFracBits + div());
    set_pcw(static_cast<uint32_t>(pcw_value / kPllSrcClk));
    return *this;
  }

  DEF_BIT(31, change);
  DEF_FIELD(26, 24, div);
  DEF_FIELD(20, 0, pcw);
};

class PmicCmd : public hwreg::RegisterBase<PmicCmd, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PmicCmd>(0xa0); }

  DEF_BIT(31, write);
  DEF_FIELD(30, 16, addr);
  DEF_FIELD(15, 0, data);
};

class PmicReadData : public hwreg::RegisterBase<PmicReadData, uint32_t> {
 public:
  static constexpr uint32_t kStateIdle = 0;
  static constexpr uint32_t kStateValid = 6;

  static auto Get() { return hwreg::RegisterAddr<PmicReadData>(0xa4); }

  DEF_FIELD(18, 16, status);
  DEF_FIELD(15, 0, data);
};

class TempMonCtl0 : public hwreg::RegisterBase<TempMonCtl0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMonCtl0>(0x00); }

  DEF_BIT(3, sense3_en);
  DEF_BIT(2, sense2_en);
  DEF_BIT(1, sense1_en);
  DEF_BIT(0, sense0_en);

  TempMonCtl0& disable_all() {
    set_sense0_en(0);
    set_sense1_en(0);
    set_sense2_en(0);
    set_sense3_en(0);
    return *this;
  }

  TempMonCtl0& enable_all() {
    set_sense0_en(1);
    set_sense1_en(1);
    set_sense2_en(1);
    set_sense3_en(1);
    return *this;
  }

  TempMonCtl0& enable_real() {
    set_sense0_en(1);
    set_sense1_en(1);
    set_sense2_en(1);
    return *this;
  }
};

class TempMonCtl1 : public hwreg::RegisterBase<TempMonCtl1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMonCtl1>(0x04); }

  DEF_FIELD(9, 0, period);
};

class TempMonCtl2 : public hwreg::RegisterBase<TempMonCtl2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMonCtl2>(0x08); }

  DEF_FIELD(25, 16, filt_interval);
  DEF_FIELD(9, 0, sen_interval);
};

class TempMonInt : public hwreg::RegisterBase<TempMonInt, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMonInt>(0x0c); }

  DEF_BIT(31, stage_3_en);
  DEF_BIT(30, stage_2_en);
  DEF_BIT(29, stage_1_en);

  DEF_BIT(14, hot_to_normal_en_2);
  DEF_BIT(13, high_offset_en_2);
  DEF_BIT(12, low_offset_en_2);
  DEF_BIT(11, hot_en_2);
  DEF_BIT(10, cold_en_2);

  DEF_BIT(9, hot_to_normal_en_1);
  DEF_BIT(8, high_offset_en_1);
  DEF_BIT(7, low_offset_en_1);
  DEF_BIT(6, hot_en_1);
  DEF_BIT(5, cold_en_1);

  DEF_BIT(4, hot_to_normal_en_0);
  DEF_BIT(3, high_offset_en_0);
  DEF_BIT(2, low_offset_en_0);
  DEF_BIT(1, hot_en_0);
  DEF_BIT(0, cold_en_0);
};

class TempMonIntStatus : public hwreg::RegisterBase<TempMonIntStatus, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMonIntStatus>(0x10); }

  DEF_BIT(31, stage_3);
  DEF_BIT(30, stage_2);
  DEF_BIT(29, stage_1);

  DEF_BIT(14, hot_to_normal_2);
  DEF_BIT(13, high_offset_2);
  DEF_BIT(12, low_offset_2);
  DEF_BIT(11, hot_2);
  DEF_BIT(10, cold_2);

  DEF_BIT(9, hot_to_normal_1);
  DEF_BIT(8, high_offset_1);
  DEF_BIT(7, low_offset_1);
  DEF_BIT(6, hot_1);
  DEF_BIT(5, cold_1);

  DEF_BIT(4, hot_to_normal_0);
  DEF_BIT(3, high_offset_0);
  DEF_BIT(2, low_offset_0);
  DEF_BIT(1, hot_0);
  DEF_BIT(0, cold_0);
};

class TempHotToNormalThreshold : public hwreg::RegisterBase<TempHotToNormalThreshold, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempHotToNormalThreshold>(0x24); }
  DEF_FIELD(11, 0, threshold);
};

class TempHotThreshold : public hwreg::RegisterBase<TempHotThreshold, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempHotThreshold>(0x28); }
  DEF_FIELD(11, 0, threshold);
};

class TempColdThreshold : public hwreg::RegisterBase<TempColdThreshold, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempColdThreshold>(0x2c); }
  DEF_FIELD(11, 0, threshold);
};

class TempOffsetHighThreshold : public hwreg::RegisterBase<TempOffsetHighThreshold, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempOffsetHighThreshold>(0x30); }
  DEF_FIELD(11, 0, threshold);
};

class TempOffsetLowThreshold : public hwreg::RegisterBase<TempOffsetLowThreshold, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempOffsetLowThreshold>(0x30); }
  DEF_FIELD(11, 0, threshold);
};

class TempMsrCtl0 : public hwreg::RegisterBase<TempMsrCtl0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMsrCtl0>(0x38); }

  static constexpr uint32_t kSample1 = 0;
  static constexpr uint32_t kSample2 = 1;
  static constexpr uint32_t kSample4Drop2 = 2;
  static constexpr uint32_t kSample6Drop2 = 3;
  static constexpr uint32_t kSample10Drop2 = 4;
  static constexpr uint32_t kSample18Drop2 = 5;

  DEF_FIELD(11, 9, msrctl3);
  DEF_FIELD(8, 6, msrctl2);
  DEF_FIELD(5, 3, msrctl1);
  DEF_FIELD(2, 0, msrctl0);
};

class TempMsrCtl1 : public hwreg::RegisterBase<TempMsrCtl1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempMsrCtl1>(0x3c); }

  DEF_BIT(8, pause_3);
  DEF_BIT(3, pause_2);
  DEF_BIT(2, pause_1);
  DEF_BIT(1, pause_0);

  TempMsrCtl1& pause_real() {
    set_pause_0(1);
    set_pause_1(1);
    set_pause_2(1);
    return *this;
  }

  TempMsrCtl1& resume_real() {
    set_pause_0(0);
    set_pause_1(0);
    set_pause_2(0);
    return *this;
  }
};

class TempAhbPoll : public hwreg::RegisterBase<TempAhbPoll, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAhbPoll>(0x40); }
};

class TempAhbTimeout : public hwreg::RegisterBase<TempAhbTimeout, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAhbTimeout>(0x44); }
};

class TempAdcPnp : public hwreg::RegisterBase<TempAdcPnp, uint32_t> {
 public:
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<TempAdcPnp>(0x48 + (index * 4)); }
};

class TempAdcMux : public hwreg::RegisterBase<TempAdcMux, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcMux>(0x54); }
};

class TempAdcEn : public hwreg::RegisterBase<TempAdcEn, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcEn>(0x60); }
};

class TempPnpMuxAddr : public hwreg::RegisterBase<TempPnpMuxAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempPnpMuxAddr>(0x64); }
};

class TempAdcMuxAddr : public hwreg::RegisterBase<TempAdcMuxAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcMuxAddr>(0x68); }
};

class TempAdcEnAddr : public hwreg::RegisterBase<TempAdcEnAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcEnAddr>(0x74); }
};

class TempAdcValidAddr : public hwreg::RegisterBase<TempAdcValidAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcValidAddr>(0x78); }
};

class TempAdcVoltAddr : public hwreg::RegisterBase<TempAdcVoltAddr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcVoltAddr>(0x7c); }
};

class TempRdCtrl : public hwreg::RegisterBase<TempRdCtrl, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempRdCtrl>(0x80); }

  static constexpr uint32_t kValidVoltageSame = 0;
  static constexpr uint32_t kValidVoltageDiff = 1;

  DEF_BIT(0, diff);
};

class TempAdcValidMask : public hwreg::RegisterBase<TempAdcValidMask, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcValidMask>(0x84); }

  static constexpr uint32_t kActiveLow = 0;
  static constexpr uint32_t kActiveHigh = 1;

  DEF_BIT(5, polarity);
  DEF_FIELD(4, 0, pos);
};

class TempAdcVoltageShift : public hwreg::RegisterBase<TempAdcVoltageShift, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcVoltageShift>(0x88); }

  DEF_FIELD(4, 0, shift);
};

class TempAdcWriteCtrl : public hwreg::RegisterBase<TempAdcWriteCtrl, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempAdcWriteCtrl>(0x8c); }

  DEF_BIT(1, mux_write_en);
  DEF_BIT(0, pnp_write_en);
};

class TempMsr : public hwreg::RegisterBase<TempMsr, uint32_t> {
 public:
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<TempMsr>(0x90 + (index * 4)); }

  DEF_BIT(15, valid);
  DEF_FIELD(11, 0, reading);
};

class TempProtCtl : public hwreg::RegisterBase<TempProtCtl, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempProtCtl>(0xc0); }

  static constexpr uint32_t kStrategyAverage = 0;
  static constexpr uint32_t kStrategyMaximum = 1;
  static constexpr uint32_t kStrategySelected = 2;

  DEF_FIELD(19, 18, sensor);
  DEF_FIELD(17, 16, strategy);
  DEF_FIELD(11, 0, offset);
};

class TempProtStage3 : public hwreg::RegisterBase<TempProtStage3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempProtStage3>(0xcc); }

  DEF_FIELD(11, 0, threshold);
};

class TempSpare : public hwreg::RegisterBase<TempSpare, uint32_t> {
 public:
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<TempSpare>(0xf0 + (index * 4)); }
};

// The following classes represent the temperature calibration fuses.
class TempCalibration0 : public hwreg::RegisterBase<TempCalibration0, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempCalibration0>(0x180); }

  DEF_FIELD(31, 26, slope);
  DEF_FIELD(25, 17, vts0);
  DEF_FIELD(16, 8, vts1);
  DEF_BIT(7, slope_sign);
  DEF_FIELD(6, 1, temp_offset);
  DEF_BIT(0, calibration_en);

  uint32_t get_vts0() const { return vts0() + kVtsOffset; }
  uint32_t get_vts1() const { return vts1() + kVtsOffset; }

 private:
  static constexpr uint32_t kVtsOffset = 3350;
};

class TempCalibration1 : public hwreg::RegisterBase<TempCalibration1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempCalibration1>(0x184); }

  DEF_FIELD(31, 22, adc_gain);
  DEF_FIELD(21, 12, adc_offset);
  DEF_BIT(2, id);

  int32_t get_adc_gain() const { return adc_gain() - kAdcCalOffset; }
  int32_t get_adc_offset() const { return adc_offset() - kAdcCalOffset; }

 private:
  static constexpr int32_t kAdcCalOffset = 512;
};

class TempCalibration2 : public hwreg::RegisterBase<TempCalibration2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<TempCalibration2>(0x188); }

  DEF_FIELD(31, 23, vts2);
  DEF_FIELD(22, 14, vts3);

  uint32_t get_vts2() const { return vts2() + kVtsOffset; }
  uint32_t get_vts3() const { return vts3() + kVtsOffset; }

 private:
  static constexpr uint32_t kVtsOffset = 3350;
};

// The following classes represent registers in the (undocumented) INFRACFG block.
class InfraCfgClkMux : public hwreg::RegisterBase<InfraCfgClkMux, uint32_t> {
 public:
  static constexpr uint32_t kIfrClk26M = 0;
  static constexpr uint32_t kIfrClkArmPll = 1;
  static constexpr uint32_t kIfrClkUnivPll = 2;
  static constexpr uint32_t kIfrClkMainPllDiv2 = 3;

  static auto Get() { return hwreg::RegisterAddr<InfraCfgClkMux>(0x00); }

  DEF_FIELD(3, 2, ifr_mux_sel);
};

// The following classes represent registers on the MT6392 PMIC.
class VprocCon10 : public hwreg::RegisterBase<VprocCon10, uint16_t> {
 private:
  static constexpr uint16_t kMaxVoltageStep = 0x7f;
  static constexpr uint32_t kVoltageStepUv = 6250;

 public:
  static constexpr uint32_t kMinVoltageUv = 700000;
  static constexpr uint32_t kMaxVoltageUv = kMinVoltageUv + (kVoltageStepUv * kMaxVoltageStep);

  static auto Get() { return hwreg::RegisterAddr<VprocCon10>(0x110); }

  uint32_t voltage() const { return (voltage_step() * kVoltageStepUv) + kMinVoltageUv; }

  VprocCon10& set_voltage(uint32_t volt_uv) {
    set_voltage_step(static_cast<uint16_t>((volt_uv - kMinVoltageUv) / kVoltageStepUv));
    return *this;
  }

  DEF_FIELD(6, 0, voltage_step);
};

}  // namespace thermal
