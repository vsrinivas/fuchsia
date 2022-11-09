// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pch-engine.h"

#include <lib/mmio/mmio-buffer.h>

#include "gtest/gtest.h"
#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"

namespace i915_tgl {

namespace {

// 24MHz, from IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705.
constexpr uint32_t kKabyLakeStandardRawClock = 0b0000'0000'0000'0000'0000'0000'0001'1000;

// 12MHz, theoretical.
constexpr uint32_t kKabyLakeHalfRawClock = 0b0000'0000'0000'0000'0000'0000'0000'1100;

// 19.2MHz. Based on IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1083 but the bits
// don't match. See PchEngine::RawClock() documentation for justification.
constexpr uint32_t kTigerLakeStandardRawClock = 0b0001'0000'0001'0010'0000'1000'0000'0000;

// 24.0MHz. Based on IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1083 but the bits
// don't match. See PchEngine::RawClock() documentation for justification.
constexpr uint32_t kTigerLakeAlternateRawClock = 0b0000'0000'0001'0111'0000'0000'0000'0000;

// 38.4MHz, from IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1131.
constexpr uint32_t kDg1StandardRawClock = 0b0001'0000'0010'0101'0001'0000'0000'0000;

// Maximum value that can be read (theoretical, breaks documented invariants).
// 1031MHz: Divider = 1024 (1023 + 1), Denominator = 7, Numerator = 1 (0 + 1).
constexpr uint32_t kTigerLakeMaxRawClock = 0b0000'0011'1111'1111'0011'1000'0000'0000;

// 100us, from IHD-OS-SKL-Vol 2c-05.16 Part 2 page 628.
constexpr uint32_t kKabyLakeStandardPpDivisor = 0x0004'af00;

// 50us assuming standard clock, theoretical.
constexpr uint32_t kKabyLakeDoublePpDivisor = 0x0009'5f00;

constexpr int kSChicken1Offset = 0xc2000;
constexpr int kSFuseStrapOffset = 0xc2014;
constexpr int kRawClkOffset = 0xc6204;
constexpr int kPpStatusOffset = 0xc7200;
constexpr int kPpControlOffset = 0xc7204;
constexpr int kPpOnDelays = 0xc7208;
constexpr int kPpOffDelays = 0xc720c;
constexpr int kPpDivisor = 0xc7210;
constexpr int kSblcPwmCtl1Offset = 0xc8250;
constexpr int kSblcPwmCtl2Offset = 0xc8254;
constexpr int kSblcPwmFreqOffset = 0xc8254;
constexpr int kSblcPwmDutyOffset = 0xc8258;
constexpr int kNdeRstWrnOpt = 0x46408;

constexpr int kAtlasGpuDeviceId = 0x591c;
constexpr int kNuc7GpuDeviceId = 0x5916;
constexpr int kDell5420GpuDeviceId = 0x9a49;

TEST(PchClockParametersTest, Equality) {
  static constexpr PchClockParameters lhs = {
      .raw_clock_hz = 24'000'000,
      .panel_power_clock_hz = 10'000,
  };

  PchClockParameters rhs = lhs;
  EXPECT_EQ(lhs, rhs);

  rhs = lhs;
  rhs.raw_clock_hz = 24'000'001;
  EXPECT_NE(lhs, rhs);

  rhs = lhs;
  rhs.panel_power_clock_hz = 10'001;
  EXPECT_NE(lhs, rhs);
}

TEST(PchPanelParametersTest, Equality) {
  // The parameters are inspired from the eDP and SPWG standards, but are
  // tweaked so each delay is unique. This is intended to help catch bugs where
  // fields are compared incorrectly.
  static constexpr PchPanelParameters lhs = {
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 900'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  };

  PchPanelParameters rhs = lhs;
  EXPECT_EQ(lhs, lhs);

  rhs = lhs;
  rhs.power_on_to_hpd_aux_ready_delay_micros = 90'001;
  EXPECT_NE(lhs, rhs);

  rhs = lhs;
  rhs.power_on_to_backlight_on_delay_micros = 260'001;
  EXPECT_NE(lhs, rhs);

  rhs = lhs;
  rhs.backlight_off_to_video_end_delay_micros = 200'001;
  EXPECT_NE(lhs, rhs);

  rhs = lhs;
  rhs.video_end_to_power_off_delay_micros = 500'001;
  EXPECT_NE(lhs, rhs);
  rhs = lhs;

  rhs.power_cycle_delay_micros = 900'001;
  EXPECT_NE(lhs, rhs);
  rhs = lhs;

  rhs.backlight_pwm_frequency_hz = 1'001;
  EXPECT_NE(lhs, rhs);
  rhs = lhs;

  rhs.power_down_on_reset = false;
  EXPECT_NE(lhs, rhs);

  rhs.backlight_pwm_inverted = true;
  EXPECT_NE(lhs, rhs);
}

TEST(PchPanelPowerTargetTest, Equality) {
  // This struct has many bit fields, so checking for mismatched bits requires a
  // different approach from above.
  static constexpr PchPanelPowerTarget lhs = {
      .power_on = false,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  };

  PchPanelPowerTarget rhs = lhs;
  EXPECT_EQ(lhs, rhs);

  rhs = lhs;
  rhs.power_on = true;
  EXPECT_NE(lhs, rhs);
  EXPECT_EQ(rhs, rhs);

  rhs = lhs;
  rhs.backlight_on = true;
  EXPECT_NE(lhs, rhs);
  EXPECT_EQ(rhs, rhs);

  rhs = lhs;
  rhs.force_power_on = true;
  EXPECT_NE(lhs, rhs);
  EXPECT_EQ(rhs, rhs);

  rhs = lhs;
  rhs.brightness_pwm_counter_on = true;
  EXPECT_NE(lhs, rhs);
  EXPECT_EQ(rhs, rhs);
}

class PchEngineTest : public ::testing::Test {
 public:
  PchEngineTest() = default;
  ~PchEngineTest() override = default;

  void SetUp() override {}
  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  constexpr static int kMmioRangeSize = 0x100000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

TEST_F(PchEngineTest, KabyLakeZeroedRegisters) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kPpDivisor, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(0, clock_parameters.raw_clock_hz);
  EXPECT_EQ(0, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(0, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, TigerLakeZeroedRegisters) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmFreqOffset, .value = 0},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(0, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, KabyLakeNuc7BootloaderConfig) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0x18},
      {.address = kPpControlOffset, .value = 0x00},
      {.address = kPpOnDelays, .value = 0x0000'0000},
      {.address = kPpOffDelays, .value = 0x0000'0000},
      {.address = kPpDivisor, .value = 0x0004'af00},
      {.address = kSblcPwmCtl1Offset, .value = 0x0000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0x0000'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kNuc7GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(0, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, KabyLakeAtlasBootloaderConfig) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0x18},
      {.address = kPpControlOffset, .value = 0x07},
      {.address = kPpOnDelays, .value = 0x0000'0000},
      {.address = kPpOffDelays, .value = 0x01f4'0000},
      {.address = kPpDivisor, .value = 0x0004'af06},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0x1d4c'1d4c},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(true, power_target.power_on);
  EXPECT_EQ(true, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(true, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(50'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(200, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(true, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(1.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, KabyLakeAtlasSecureBootloaderConfig) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0x18},
      {.address = kPpControlOffset, .value = 0x08},
      {.address = kPpOnDelays, .value = 0x0000'0000},
      {.address = kPpOffDelays, .value = 0x0000'0000},
      {.address = kPpDivisor, .value = 0x0004'af00},
      {.address = kSblcPwmCtl1Offset, .value = 0x0000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0x0000'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(true, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(0, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, TigerLakeDell5420BootloaderConfig) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0x901},
      {.address = kRawClkOffset, .value = 0x1012'0800},
      {.address = kPpControlOffset, .value = 0x67},
      {.address = kPpOnDelays, .value = 0x0001'0001},
      {.address = kPpOffDelays, .value = 0x01f4'0001},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0x0001'7700},
      {.address = kSblcPwmDutyOffset, .value = 0x0001'7700},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(19'200'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(true, power_target.power_on);
  EXPECT_EQ(true, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(true, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(100, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(100, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(100, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(50'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(200, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(true, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, TigerLakeNuc11BootloaderConfig) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0x900},
      {.address = kRawClkOffset, .value = 0x1012'0800},
      {.address = kPpControlOffset, .value = 0x08},
      {.address = kPpOnDelays, .value = 0x0000'0000},
      {.address = kPpOffDelays, .value = 0x0000'0000},
      {.address = kSblcPwmCtl1Offset, .value = 0x0000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0x0000'0000},
      {.address = kSblcPwmDutyOffset, .value = 0x0000'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(19'200'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(true, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(0, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(0, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(0, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(0, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);

  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTest, KabyLakeRestoreClockParameters) {
  // The register values are based on real values, and slightly modified to
  // catch register-swapping bugs.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0x18},
      {.address = kPpControlOffset, .value = 0x07},
      {.address = kPpOnDelays, .value = 0x0001'0001},
      {.address = kPpOffDelays, .value = 0x01f4'0000},
      {.address = kPpDivisor, .value = 0x0004'af06},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0x1d4c'1d4c},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = 0x18, .write = true},
      {.address = kPpDivisor, .value = 0x0004'af06, .write = true},
      {.address = kSChicken1Offset, .value = 0, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.RestoreClockParameters();
}

TEST_F(PchEngineTest, TigerLakeRestoreClockParameters) {
  // The register values are based on real values, and slightly modified to
  // catch register-swapping bugs.
  //
  // S_CHICKEN1 has bit 7 set to check that RestoreClockParameters() implements
  // the workaround that requires resetting that bit.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0x981},
      {.address = kRawClkOffset, .value = 0x1012'0800},
      {.address = kPpControlOffset, .value = 0x67},
      {.address = kPpOnDelays, .value = 0x0001'0001},
      {.address = kPpOffDelays, .value = 0x01f4'0001},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0x0001'7700},
      {.address = kSblcPwmDutyOffset, .value = 0x0001'7700},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = 0x1012'0800, .write = true},
      {.address = kSChicken1Offset, .value = 0x901, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.RestoreClockParameters();
}

TEST_F(PchEngineTest, KabyLakeRestoreNonClockParameters) {
  // The register values are based on real values, and slightly modified to
  // catch register-swapping bugs.
  //
  // PP_CONTROL bits 0 and 2 and SBLC_PWM_CTL1 bit 31 are set to check that
  // RestoreParameters() turn off panel power and disable the backlight PWM.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = 0x18},
      {.address = kPpControlOffset, .value = 0x07},
      {.address = kPpOnDelays, .value = 0x0001'0001},
      {.address = kPpOffDelays, .value = 0x01f4'0000},
      {.address = kPpDivisor, .value = 0x0004'af06},
      {.address = kSblcPwmCtl1Offset, .value = 0xa000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0x1d4c'122c},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0001'0001, .write = true},
      {.address = kPpOffDelays, .value = 0x01f4'0000, .write = true},
      {.address = kPpControlOffset, .value = 0x02, .write = true},
      {.address = kSblcPwmCtl2Offset, .value = 0x1d4c'122c, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.RestoreNonClockParameters();
}

TEST_F(PchEngineTest, TigerLakeRestoreNonClockParameters) {
  // The register values are based on real values, and slightly modified to
  // catch register-swapping bugs.
  //
  // PP_CONTROL bits 0 and 2 and SBLC_PWM_CTL1 bit 31 are set to check that
  // RestoreParameters() turn off panel power and disable the backlight PWM.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0x981},
      {.address = kRawClkOffset, .value = 0x1012'0800},
      {.address = kPpControlOffset, .value = 0x67},
      {.address = kPpOnDelays, .value = 0x0001'0001},
      {.address = kPpOffDelays, .value = 0x01f4'0001},
      {.address = kSblcPwmCtl1Offset, .value = 0xa000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0x0001'7700},
      {.address = kSblcPwmDutyOffset, .value = 0x0001'2200},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0001'0001, .write = true},
      {.address = kPpOffDelays, .value = 0x01f4'0001, .write = true},
      {.address = kPpControlOffset, .value = 0x62, .write = true},
      {.address = kSblcPwmFreqOffset, .value = 0x0001'7700, .write = true},
      {.address = kSblcPwmDutyOffset, .value = 0x0001'2200, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.RestoreNonClockParameters();
}

class PchEngineResetHandshakeTest : public PchEngineTest {
 public:
  // Set up expectations for PCH registers.
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor},
        {.address = kSblcPwmCtl1Offset, .value = 0},
        {.address = kSblcPwmCtl2Offset, .value = 0},
    }));
  }
};

TEST_F(PchEngineResetHandshakeTest, GenericSetPchResetHandshakeEnabled) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kNdeRstWrnOpt, .value = 0},
      {.address = kNdeRstWrnOpt, .value = 0x10, .write = true},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPchResetHandshake(true);
}

TEST_F(PchEngineResetHandshakeTest, GenericSetPchResetHandshakeEnabledNoChange) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kNdeRstWrnOpt, .value = 0},
      {.address = kNdeRstWrnOpt, .value = 0x10, .write = true},
      {.address = kNdeRstWrnOpt, .value = 0x10},
      {.address = kNdeRstWrnOpt, .value = 0x10},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPchResetHandshake(true);
  pch_engine.SetPchResetHandshake(true);  // No MMIO writes.
  pch_engine.SetPchResetHandshake(true);  // No MMIO writes.
}

TEST_F(PchEngineResetHandshakeTest, GenericSetPchResetHandshakeEnabledFromAtlasBootloaderState) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kNdeRstWrnOpt, .value = 0x30},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPchResetHandshake(true);  // No MMIO writes.
}

TEST_F(PchEngineResetHandshakeTest, GenericSetPchResetHandshakeDisabled) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kNdeRstWrnOpt, .value = 0xff},
      {.address = kNdeRstWrnOpt, .value = 0xef, .write = true},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPchResetHandshake(false);
}

TEST_F(PchEngineResetHandshakeTest, GenericSetPchResetHandshakeDisabledNoChange) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kNdeRstWrnOpt, .value = 0xff},
      {.address = kNdeRstWrnOpt, .value = 0xef, .write = true},
      {.address = kNdeRstWrnOpt, .value = 0xef},
      {.address = kNdeRstWrnOpt, .value = 0xef},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPchResetHandshake(false);
  pch_engine.SetPchResetHandshake(false);  // No MMIO writes.
  pch_engine.SetPchResetHandshake(false);  // No MMIO writes.
}

class PchEngineKabyLakeClockTest : public PchEngineTest {
 public:
  // Set up expectations for registers except for RAWCLK_FREQ and PP_DIVISOR.
  template <typename Lambda1, typename Lambda2>
  void SetPchMmioExpectations(Lambda1 raw_clock_expectations, Lambda2 panel_divisor_expectations) {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
    }));
    raw_clock_expectations();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
    }));
    panel_divisor_expectations();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSblcPwmCtl1Offset, .value = 0},
        {.address = kSblcPwmCtl2Offset, .value = 0},
    }));
  }
};

TEST_F(PchEngineKabyLakeClockTest, StandardClockStandardDivisor) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = kKabyLakeStandardRawClock}); },
      [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, HalfClockStandardDivisor) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = kKabyLakeHalfRawClock}); },
      [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(12'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(5'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, StandardClockDoubleDivisor) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = kKabyLakeStandardRawClock}); },
      [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = kKabyLakeDoublePpDivisor}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(5'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, Zeros) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(0, clock_parameters.raw_clock_hz);
  EXPECT_EQ(0, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, Ones) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0x0000'03ff}); },
      [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0xffff'ff00}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'023'000'000, clock_parameters.raw_clock_hz);

  // 30 is 1,023,000,000 / (2 ** 25).
  EXPECT_EQ(30, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, SetStandardClockStandardDivisor) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock, .write = true},
      {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 24'000'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineKabyLakeClockTest, SetStandardClockStandardDivisorNoChange) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = kKabyLakeStandardRawClock}); },
      [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor}); });

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 24'000'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineKabyLakeClockTest, SetHalfClockDoubleDivisor) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = kKabyLakeHalfRawClock, .write = true},
      {.address = kPpDivisor, .value = kKabyLakeDoublePpDivisor, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 12'000'000,
      .panel_power_clock_hz = 2'500,
  });
}

TEST_F(PchEngineKabyLakeClockTest, SetRawClockOverflow) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = 0x0000'03ff, .write = true},
      {.address = kPpDivisor, .value = 0xffff'ff00, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 0x7fff'ffff,
      .panel_power_clock_hz = 1,
  });

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'023'000'000, clock_parameters.raw_clock_hz);

  // 30 is 1,023,000,000 / (2 ** 25).
  EXPECT_EQ(30, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, SetDivisorUnderflow) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = 0x0000'0001, .write = true},
      {.address = kPpDivisor, .value = 0x0000'0100, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 1'000'000,
      .panel_power_clock_hz = 500'000,
  });

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(250'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineKabyLakeClockTest, FixClockParameters) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); },
                         [&]() { mmio_range_.Expect({.address = kPpDivisor, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(0, clock_parameters.raw_clock_hz);
  EXPECT_EQ(0, clock_parameters.panel_power_clock_hz);

  pch_engine.FixClockParameters(clock_parameters);
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

class PchEngineTigerLakeClockParametersTest : public PchEngineTest {
 public:
  // Set up expectations for registers except for RAWCLK_FREQ.
  template <typename Lambda>
  void SetPchMmioExpectations(Lambda raw_clock_expectations) {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
    }));
    raw_clock_expectations();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kSblcPwmCtl1Offset, .value = 0},
        {.address = kSblcPwmFreqOffset, .value = 0},
        {.address = kSblcPwmDutyOffset, .value = 0},
    }));
  }
};

TEST_F(PchEngineTigerLakeClockParametersTest, StandardClock) {
  SetPchMmioExpectations([&]() {
    mmio_range_.Expect({.address = kRawClkOffset, .value = kTigerLakeStandardRawClock});
  });
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(19'200'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, AlternateClock) {
  SetPchMmioExpectations([&]() {
    mmio_range_.Expect({.address = kRawClkOffset, .value = kTigerLakeAlternateRawClock});
  });
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, Dg1StandardClock) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = kDg1StandardRawClock}); });
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(38'400'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, Zeros) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, Ones) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0xffff'ffff}); });
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();

  // Integer part = 1024 (1023 + 1), numerator = 7, denominator = 16 (15 + 1).
  EXPECT_EQ(1'024'437'500, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, SetStandardClock) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 19'200'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineTigerLakeClockParametersTest, SetStandardClockNoChange) {
  SetPchMmioExpectations([&]() {
    mmio_range_.Expect({.address = kRawClkOffset, .value = kTigerLakeStandardRawClock});
  });

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 19'200'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineTigerLakeClockParametersTest, SetAlternateClock) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = kTigerLakeAlternateRawClock, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 24'000'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineTigerLakeClockParametersTest, SetDg1StandardClock) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = kDg1StandardRawClock, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      .raw_clock_hz = 38'400'000,
      .panel_power_clock_hz = 10'000,
  });
}

TEST_F(PchEngineTigerLakeClockParametersTest, SetOverflow) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kRawClkOffset, .value = 0x1fff'3800, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetClockParameters(PchClockParameters{
      // Maximum 31-bit integer value congruent to 999,999 modulo 1,000,000.
      .raw_clock_hz = 0x7ff89ebf,
      .panel_power_clock_hz = 10'000,
  });

  const PchClockParameters clock_parameters = pch_engine.ClockParameters();
  // Integer = 1024 (1023 + 1), numerator = 7, denominator = 8 (7 + 1).
  EXPECT_EQ(1'024'875'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, FixClockParametersToStandardRawClock) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });
  mmio_range_.Expect({.address = kSFuseStrapOffset, .value = 0});

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  pch_engine.FixClockParameters(clock_parameters);
  EXPECT_EQ(19'200'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

TEST_F(PchEngineTigerLakeClockParametersTest, FixClockParametersToAlternateRawClock) {
  SetPchMmioExpectations([&]() { mmio_range_.Expect({.address = kRawClkOffset, .value = 0}); });
  mmio_range_.Expect({.address = kSFuseStrapOffset, .value = 0x0000'0100});

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  PchClockParameters clock_parameters = pch_engine.ClockParameters();
  EXPECT_EQ(1'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);

  pch_engine.FixClockParameters(clock_parameters);
  EXPECT_EQ(24'000'000, clock_parameters.raw_clock_hz);
  EXPECT_EQ(10'000, clock_parameters.panel_power_clock_hz);
}

class PchEnginePanelPowerTargetTest : public PchEngineTest {
 public:
  // Set up expectations for registers except for PP_CONTROL and SBLC_PWM_CTL1.
  template <typename Lambda1, typename Lambda2>
  void SetPchMmioExpectations(Lambda1 power_control_expectations,
                              Lambda2 backlight_control_expectations) {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
    }));
    power_control_expectations();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor},
    }));
    backlight_control_expectations();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSblcPwmCtl2Offset, .value = 0},
    }));
  }
};

TEST_F(PchEnginePanelPowerTargetTest, GenericAllFlagsOff) {
  SetPchMmioExpectations(
      // The bits around the control flags are on to catch bit mapping errors.
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0xf2}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);
}

TEST_F(PchEnginePanelPowerTargetTest, GenericPowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(true, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);
}

TEST_F(PchEnginePanelPowerTargetTest, GenericBacklightOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x04}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(true, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);
}

TEST_F(PchEnginePanelPowerTargetTest, GenericForcePowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x08}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(true, power_target.force_power_on);
  EXPECT_EQ(false, power_target.brightness_pwm_counter_on);
}

TEST_F(PchEnginePanelPowerTargetTest, GenericBrightnessPwmCounterOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0x8000'0000}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelPowerTarget power_target = pch_engine.PanelPowerTarget();
  EXPECT_EQ(false, power_target.power_on);
  EXPECT_EQ(false, power_target.backlight_on);
  EXPECT_EQ(false, power_target.force_power_on);
  EXPECT_EQ(true, power_target.brightness_pwm_counter_on);
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x00}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOff) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x00, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = false,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOnFromForcePowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x08}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOnNoChange) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetBacklightOnFromPowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x05, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = true,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetBacklightOnBrightnessPwmOnFromPowerOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpControlOffset, .value = 0x05, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000, .write = true},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = true,
      .force_power_on = false,
      .brightness_pwm_counter_on = true,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOnBacklightOnBrightnessPwmOnNoChange) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x05}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0x8000'0000}); });
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = true,
      .force_power_on = false,
      .brightness_pwm_counter_on = true,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetPowerOnBacklightOn) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x00}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x05, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = true,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetBacklightOff) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x05}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x01, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetBacklightOffBrightnessPwmOff) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x05}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0xa000'0000}); });

  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpControlOffset, .value = 0x01, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = true,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEnginePanelPowerTargetTest, GenericSetForcePowerOff) {
  SetPchMmioExpectations(
      [&]() { mmio_range_.Expect({.address = kPpControlOffset, .value = 0x08}); },
      [&]() { mmio_range_.Expect({.address = kSblcPwmCtl1Offset, .value = 0}); });

  mmio_range_.Expect({.address = kPpControlOffset, .value = 0x00, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  pch_engine.SetPanelPowerTarget(PchPanelPowerTarget{
      .power_on = false,
      .backlight_on = false,
      .force_power_on = false,
      .brightness_pwm_counter_on = false,
  });
}

TEST_F(PchEngineTest, KabyLakePanelParameters) {
  // The parameters are inspired from the eDP and SPWG standards, but are
  // tweaked so each delay is unique. This is intended to help catch bugs where
  // values are incorrectly mapped to register fields.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},

      // The bits around `power_down_on_reset` are set, to catch mapping errors.
      {.address = kPpControlOffset, .value = 0x05},

      {.address = kPpOnDelays, .value = 0x0384'0a28},
      {.address = kPpOffDelays, .value = 0x1388'07d0},
      {.address = kPpDivisor, .value = 0x0004'af0a},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(90'000, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(260'000, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(200'000, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(900'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1'000, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, KabyLakePanelParametersPowerDownOnResetEnabled) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},

      // Only `power_down_on_reset` is set, to catch mapping errors.
      {.address = kPpControlOffset, .value = 0x02},

      {.address = kPpOnDelays, .value = 0x0384'0a28},
      {.address = kPpOffDelays, .value = 0x1388'07d0},
      {.address = kPpDivisor, .value = 0x0004'af0a},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(90'000, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(260'000, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(200'000, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(900'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1'000, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(true, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, KabyLakePanelParametersBacklightPwmInverted) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0x05},
      {.address = kPpOnDelays, .value = 0x0384'0a28},
      {.address = kPpOffDelays, .value = 0x1388'07d0},
      {.address = kPpDivisor, .value = 0x0004'af0a},

      // Only `backlight_pwm_inverted` is set, to catch mapping errors.
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},

      {.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(90'000, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(260'000, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(200'000, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(900'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1'000, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(true, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, TigerLakePanelParametersStandardRawClock) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},

      // The bits around `power_down_on_reset` are set, to catch mapping errors.
      {.address = kPpControlOffset, .value = 0xc5},

      {.address = kPpOnDelays, .value = 0x0384'0a28},
      {.address = kPpOffDelays, .value = 0x1388'07d0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmFreqOffset, .value = 0x4b00},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(90'000, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(260'000, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(200'000, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(1'100'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1'000, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, TigerLakePanelParametersAlternateRawClock) {
  // The parameters are inspired from the eDP and SPWG standards, but are
  // tweaked so each delay is unique. This is intended to help catch bugs where
  // values are incorrectly mapped to register fields.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},

      // The different raw clock value must not influence how we interpret the
      // fields in the PP_* registers. The delays there are all relative to the
      // panel power sequencing clock, which is fixed to 10 KHz on Tiger Lake.
      //
      // On the other hand, the differences should impact the SBLC_* registers,
      // which are relative to the raw clock.
      {.address = kRawClkOffset, .value = kTigerLakeAlternateRawClock},

      // The bits around `power_down_on_reset` are set, to catch mapping errors.
      {.address = kPpControlOffset, .value = 0xc5},

      {.address = kPpOnDelays, .value = 0x0384'0a28},
      {.address = kPpOffDelays, .value = 0x1388'07d0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmFreqOffset, .value = 0x5dc0},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(90'000, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(260'000, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(200'000, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(500'000, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(1'100'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1'000, panel_parameters.backlight_pwm_frequency_hz);
  EXPECT_EQ(false, panel_parameters.power_down_on_reset);
  EXPECT_EQ(false, panel_parameters.backlight_pwm_inverted);
}

TEST_F(PchEngineTest, KabyLakeSetPanelParameters) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kPpDivisor, .value = 0x0004'af00},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0384'0a28, .write = true},
      {.address = kPpOffDelays, .value = 0x1388'07d0, .write = true},
      {.address = kPpDivisor, .value = 0x0004'af0a, .write = true},
      {.address = kPpControlOffset, .value = 0x02, .write = true},
      {.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x0000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  // The parameters are inspired from the eDP and SPWG standards, but are
  // tweaked so each delay is unique. This is intended to help catch bugs where
  // values are incorrectly mapped to register fields.
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 900'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

TEST_F(PchEngineTest, KabyLakeSetPanelParametersWhileBacklightPwmIsOn) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kPpDivisor, .value = 0x0004'af00},
      {.address = kSblcPwmCtl1Offset, .value = 0xa000'0000},
      {.address = kSblcPwmCtl2Offset, .value = 0},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0384'0a28, .write = true},
      {.address = kPpOffDelays, .value = 0x1388'07d0, .write = true},
      {.address = kPpDivisor, .value = 0x0004'af0a, .write = true},
      {.address = kPpControlOffset, .value = 0x02, .write = true},

      // The backlight PWM must be disabled before changing its frequency.
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},

      {.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  // Parameters from PchEngineTest.KabyLakeSetPanelParameters above.
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 900'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

// PCH panel parameters that should result in all-zero register fields.
//
// These values check for underflows in value handling. For example, setting the
// power cycle delay involves a subtraction, which is subject to underflow.
constexpr PchPanelParameters kPanelParametersZeros = {
    .power_on_to_hpd_aux_ready_delay_micros = 0,
    .power_on_to_backlight_on_delay_micros = 0,
    .backlight_off_to_video_end_delay_micros = 0,
    .video_end_to_power_off_delay_micros = 0,
    .power_cycle_delay_micros = 0,
    .backlight_pwm_frequency_hz = 0x7fff'ffff,
    .power_down_on_reset = false,
    .backlight_pwm_inverted = false,
};

class PchEngineKabyLakeSetPanelParametersZerosTest : public PchEngineTest {
 public:
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0x0a},
        {.address = kPpOnDelays, .value = 0x0001'0001},
        {.address = kPpOffDelays, .value = 0x0001'0001},
        {.address = kPpDivisor, .value = 0x0004'af06},
        {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},
        {.address = kSblcPwmCtl2Offset, .value = 0x0000'ffff},
    }));
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpOnDelays, .value = 0, .write = true},
        {.address = kPpOffDelays, .value = 0, .write = true},
        {.address = kPpDivisor, .value = 0x0004'af01, .write = true},
        {.address = kPpControlOffset, .value = 0x08, .write = true},
        {.address = kSblcPwmCtl2Offset, .value = 0x0001'0000, .write = true},
        {.address = kSblcPwmCtl1Offset, .value = 0x0000'0000, .write = true},
    }));
  }
};

TEST_F(PchEngineKabyLakeSetPanelParametersZerosTest, Once) {
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // All the MMIO assertions are in SetUp(), because they're shared with the
  // NoChange test below.
  pch_engine.SetPanelParameters(kPanelParametersZeros);
}

TEST_F(PchEngineKabyLakeSetPanelParametersZerosTest, RepeatedWithNoChange) {
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelParameters(kPanelParametersZeros);
  pch_engine.SetPanelParameters(kPanelParametersZeros);  // No MMIO writes.
}

// PCH panel parameters with maximum values.
//
// These values check for overflows in value handling.
constexpr PchPanelParameters kPanelParametersOverflow = {
    .power_on_to_hpd_aux_ready_delay_micros = 0x7fff'ffff,
    .power_on_to_backlight_on_delay_micros = 0x7fff'ffff,
    .backlight_off_to_video_end_delay_micros = 0x7fff'ffff,
    .video_end_to_power_off_delay_micros = 0x7fff'ffff,
    .power_cycle_delay_micros = 0x7fff'ffff,
    .backlight_pwm_frequency_hz = 1,
    .power_down_on_reset = true,
    .backlight_pwm_inverted = true,
};

class PchEngineKabyLakeSetPanelParametersOverflowTest : public PchEngineTest {
 public:
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = 0x0004'af00},
        {.address = kSblcPwmCtl1Offset, .value = 0},

        // These values check for overflow in brightness-preserving logic.
        {.address = kSblcPwmCtl2Offset, .value = 0x0001'ffff},
    }));
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpOnDelays, .value = 0x1fff'1fff, .write = true},
        {.address = kPpOffDelays, .value = 0x1fff'1fff, .write = true},
        {.address = kPpDivisor, .value = 0x0004'af1f, .write = true},
        {.address = kPpControlOffset, .value = 0x02, .write = true},
        {.address = kSblcPwmCtl2Offset, .value = 0xffff'ffff, .write = true},
        {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
    }));
  }
};

TEST_F(PchEngineKabyLakeSetPanelParametersOverflowTest, Once) {
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // All the MMIO assertions are in SetUp(), because they're shared with the
  // NoChange test below.
  pch_engine.SetPanelParameters(kPanelParametersOverflow);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(819'100, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(3'000'000, panel_parameters.power_cycle_delay_micros);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(23, panel_parameters.backlight_pwm_frequency_hz);
}

TEST_F(PchEngineKabyLakeSetPanelParametersOverflowTest, RepeatedWithNoChange) {
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelParameters(kPanelParametersOverflow);
  pch_engine.SetPanelParameters(kPanelParametersOverflow);  // No MMIO writes.
}

TEST_F(PchEngineTest, KabyLakeSetPanelParametersOnlyPowerDownOnReset) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kPpDivisor, .value = 0x0004'af01},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0001'0000},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpControlOffset, .value = 0x02, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 0,
      .power_on_to_backlight_on_delay_micros = 0,
      .backlight_off_to_video_end_delay_micros = 0,
      .video_end_to_power_off_delay_micros = 0,
      .power_cycle_delay_micros = 0,
      .backlight_pwm_frequency_hz = 0x7fff'ffff,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

TEST_F(PchEngineTest, KabyLakeSetPanelParametersOnlyBacklightPwmInverted) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kPpDivisor, .value = 0x0004'af01},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0001'0000},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 0,
      .power_on_to_backlight_on_delay_micros = 0,
      .backlight_off_to_video_end_delay_micros = 0,
      .video_end_to_power_off_delay_micros = 0,
      .power_cycle_delay_micros = 0,
      .backlight_pwm_frequency_hz = 0x7fff'ffff,
      .power_down_on_reset = false,
      .backlight_pwm_inverted = true,
  });
}

TEST_F(PchEngineTest, TigerLakeSetPanelParameters) {
  // PP_CONTROL is non-zero to check that control bits are mixed correctly with
  // the delay field.
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0x08},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0},
      {.address = kSblcPwmDutyOffset, .value = 0x0001},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0384'0a28, .write = true},
      {.address = kPpOffDelays, .value = 0x1388'07d0, .write = true},
      {.address = kPpControlOffset, .value = 0x8a, .write = true},
      {.address = kSblcPwmFreqOffset, .value = 0x4b00, .write = true},
      {.address = kSblcPwmDutyOffset, .value = 0, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  // The parameters are inspired from the eDP and SPWG standards, but are
  // tweaked so each delay is unique. This is intended to help catch bugs where
  // values are incorrectly mapped to register fields.
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 700'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

TEST_F(PchEngineTest, TigerLakeSetPanelParametersWhileBacklightPwmIsOn) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0x08},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0xa000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0},
      {.address = kSblcPwmDutyOffset, .value = 0x0001},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0384'0a28, .write = true},
      {.address = kPpOffDelays, .value = 0x1388'07d0, .write = true},
      {.address = kPpControlOffset, .value = 0x8a, .write = true},

      // The backlight PWM must be disabled before changing its frequency.
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},

      {.address = kSblcPwmFreqOffset, .value = 0x4b00, .write = true},
      {.address = kSblcPwmDutyOffset, .value = 0, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  // Parameters from PchEngineTest.TigerLakeSetPanelParameters above.
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 700'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

TEST_F(PchEngineTest, TigerLakeSetPanelParametersAlternateRawClock) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},

      // The different raw clock value must not influence how we interpret the
      // fields in the PP_* registers. The delays there are all relative to the
      // panel power sequencing clock, which is fixed to 10 KHz on Tiger Lake.
      //
      // On the other hand, the differences should impact the SBLC_* registers,
      // which are relative to the raw clock.
      {.address = kRawClkOffset, .value = kTigerLakeAlternateRawClock},

      {.address = kPpControlOffset, .value = 0x08},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},
      {.address = kSblcPwmFreqOffset, .value = 0},
      {.address = kSblcPwmDutyOffset, .value = 0x0001},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpOnDelays, .value = 0x0384'0a28, .write = true},
      {.address = kPpOffDelays, .value = 0x1388'07d0, .write = true},
      {.address = kPpControlOffset, .value = 0x8a, .write = true},
      {.address = kSblcPwmFreqOffset, .value = 0x5dc0, .write = true},
      {.address = kSblcPwmDutyOffset, .value = 0, .write = true},
      {.address = kSblcPwmCtl1Offset, .value = 0, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 90'000,    // eDP T1+T3 max
      .power_on_to_backlight_on_delay_micros = 260'000,    // SPWG T1+T2+T5 max/min
      .backlight_off_to_video_end_delay_micros = 200'000,  // SPWG T6 min
      .video_end_to_power_off_delay_micros = 500'000,      // eDP T10 max
      .power_cycle_delay_micros = 700'000,
      .backlight_pwm_frequency_hz = 1'000,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

class PchEngineTestTigerLakeSetPanelParametersZerosTest : public PchEngineTest {
 public:
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0x4a},
        {.address = kPpOnDelays, .value = 0x0001'0001},
        {.address = kPpOffDelays, .value = 0x0001'0001},
        {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000},

        // PWM duty cycle  > frequency divider to check that we don't write a
        // frequency that's smaller than the current Duty Cycle.
        {.address = kSblcPwmFreqOffset, .value = 0},
        {.address = kSblcPwmDutyOffset, .value = 0xffff'ffff},
    }));
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpOnDelays, .value = 0, .write = true},
        {.address = kPpOffDelays, .value = 0, .write = true},
        {.address = kPpControlOffset, .value = 0x18, .write = true},
        {.address = kSblcPwmFreqOffset, .value = 0x0000'0001, .write = true},
        {.address = kSblcPwmDutyOffset, .value = 0, .write = true},
        {.address = kSblcPwmCtl1Offset, .value = 0, .write = true},
    }));
  }
};

TEST_F(PchEngineTestTigerLakeSetPanelParametersZerosTest, Once) {
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  // All the MMIO assertions are in SetUp(), because they're shared with the
  // NoChange test below.
  pch_engine.SetPanelParameters(kPanelParametersZeros);
}

TEST_F(PchEngineTestTigerLakeSetPanelParametersZerosTest, RepeatedWithNoChange) {
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelParameters(kPanelParametersZeros);
  pch_engine.SetPanelParameters(kPanelParametersZeros);  // No MMIO writes.
}

class PchEngineTigerLakeSetPanelParametersOverflowTest : public PchEngineTest {
 public:
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kTigerLakeMaxRawClock},
        {.address = kPpControlOffset, .value = 0x08},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kSblcPwmCtl1Offset, .value = 0},

        // The frequency divider must be non-zero to get a non-zero brightness.
        {.address = kSblcPwmFreqOffset, .value = 0x0000'0001},
        // The maximum duty cycle value tests the brightness clamping logic.
        {.address = kSblcPwmDutyOffset, .value = 0xffff'ffff},
    }));
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpOnDelays, .value = 0x1fff'1fff, .write = true},
        {.address = kPpOffDelays, .value = 0x1fff'1fff, .write = true},
        {.address = kPpControlOffset, .value = 0x01fa, .write = true},
        {.address = kSblcPwmFreqOffset, .value = 0x3d73'cfc0, .write = true},
        {.address = kSblcPwmDutyOffset, .value = 0x3d73'cfc0, .write = true},
        {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
    }));
  }
};

TEST_F(PchEngineTigerLakeSetPanelParametersOverflowTest, Once) {
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);

  // All the MMIO assertions are in SetUp(), because they're shared with the
  // NoChange test below.
  pch_engine.SetPanelParameters(kPanelParametersOverflow);

  const PchPanelParameters panel_parameters = pch_engine.PanelParameters();
  EXPECT_EQ(819'100, panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.power_on_to_backlight_on_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.backlight_off_to_video_end_delay_micros);
  EXPECT_EQ(819'100, panel_parameters.video_end_to_power_off_delay_micros);
  EXPECT_EQ(3'000'000, panel_parameters.power_cycle_delay_micros);
  EXPECT_EQ(1, panel_parameters.backlight_pwm_frequency_hz);
}

TEST_F(PchEngineTigerLakeSetPanelParametersOverflowTest, RepeatedWithNoChange) {
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelParameters(kPanelParametersOverflow);
  pch_engine.SetPanelParameters(kPanelParametersOverflow);  // No MMIO writes.
}

TEST_F(PchEngineTest, TigerLakeSetPanelParametersOnlyPowerDownOnReset) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0x10},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmFreqOffset, .value = 0x0000'0001},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpControlOffset, .value = 0x12, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 0,
      .power_on_to_backlight_on_delay_micros = 0,
      .backlight_off_to_video_end_delay_micros = 0,
      .video_end_to_power_off_delay_micros = 0,
      .power_cycle_delay_micros = 0,
      .backlight_pwm_frequency_hz = 0x7fff'ffff,
      .power_down_on_reset = true,
      .backlight_pwm_inverted = false,
  });
}

TEST_F(PchEngineTest, TigerLakeSetPanelParametersOnlyBacklightPwmInverted) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},
      {.address = kPpControlOffset, .value = 0x10},
      {.address = kPpOnDelays, .value = 0},
      {.address = kPpOffDelays, .value = 0},
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmFreqOffset, .value = 0x0000'0001},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0x2000'0000, .write = true},
  }));

  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelParameters(PchPanelParameters{
      .power_on_to_hpd_aux_ready_delay_micros = 0,
      .power_on_to_backlight_on_delay_micros = 0,
      .backlight_off_to_video_end_delay_micros = 0,
      .video_end_to_power_off_delay_micros = 0,
      .power_cycle_delay_micros = 0,
      .backlight_pwm_frequency_hz = 0x7fff'ffff,
      .power_down_on_reset = false,
      .backlight_pwm_inverted = true,
  });
}

class PchEngineBrightnessPwmTest : public PchEngineTest {
 public:
  // Sets PP_* register expectations so no unrelated assertion is triggered.
  void SetKabyLakePanelPowerReadExpectations() {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor},
    }));
  }
};

TEST_F(PchEngineBrightnessPwmTest, KabyLake16IncrementMinFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0xffff0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  EXPECT_EQ(24'000'000, pch_engine.ClockParameters().raw_clock_hz);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(23, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

TEST_F(PchEngineBrightnessPwmTest, KabyLake16Increment100StepsMaxFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0064'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(15'000, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

TEST_F(PchEngineBrightnessPwmTest, KabyLake16Increment256StepsMaxFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 0},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0100'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(5'859, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

TEST_F(PchEngineBrightnessPwmTest, KabyLake128IncrementMinFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 1},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0xffff0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(3, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

TEST_F(PchEngineBrightnessPwmTest, KabyLake128Increment100StepsMaxFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 1},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0064'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(1'875, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

TEST_F(PchEngineBrightnessPwmTest, KabyLake128Increment256StepsMaxFrequency) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSChicken1Offset, .value = 1},
      {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
  }));
  SetKabyLakePanelPowerReadExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmCtl1Offset, .value = 0},
      {.address = kSblcPwmCtl2Offset, .value = 0x0100'0000},
  }));
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);

  // IHD-OS-KBL-Vol 12-1.17 page 196 and IHD-OS-SKL-Vol 12-05.16 page 189.
  EXPECT_EQ(732, pch_engine.PanelParameters().backlight_pwm_frequency_hz);
}

class PchEngineKabyLakeBrightnessTest : public PchEngineTest {
 protected:
  // Sets all the values except for SBLC_PWM_CTL2.
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 1},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor},
        {.address = kSblcPwmCtl1Offset, .value = 0x8000'0000},
    }));
  }
};

TEST_F(PchEngineKabyLakeBrightnessTest, ReadZero) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineKabyLakeBrightnessTest, ReadOne) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'05dc});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(1.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineKabyLakeBrightnessTest, ReadSmallFraction) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0177});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(0.25, pch_engine.PanelBrightness());
}

TEST_F(PchEngineKabyLakeBrightnessTest, ReadLargeFraction) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0465});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(0.75, pch_engine.PanelBrightness());
}

TEST_F(PchEngineKabyLakeBrightnessTest, WriteSmallFraction) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'05dc});
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0177, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelBrightness(0.25);
}

TEST_F(PchEngineKabyLakeBrightnessTest, WriteLargeFraction) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0000});
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0465, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelBrightness(0.75);
}

TEST_F(PchEngineKabyLakeBrightnessTest, WriteSmallFractionNoChange) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x05dc'0177});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelBrightness(0.25);
}

TEST_F(PchEngineKabyLakeBrightnessTest, WriteMisconfiguredNoChange) {
  mmio_range_.Expect({.address = kSblcPwmCtl2Offset, .value = 0x0000'1111});
  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  pch_engine.SetPanelBrightness(1.0);
}

class PchEngineTigerLakeBrightnessTest : public PchEngineTest {
 protected:
  // Sets all the values except for SBLC_PWM_DUTY and SBLC_PWM_FREQ.
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 0},
        {.address = kRawClkOffset, .value = kTigerLakeStandardRawClock},

        // The bits around `power_down_on_reset` are set, to catch mapping errors.
        {.address = kPpControlOffset, .value = 0xc5},

        {.address = kPpOnDelays, .value = 0x0384'0a28},
        {.address = kPpOffDelays, .value = 0x1388'07d0},
        {.address = kSblcPwmCtl1Offset, .value = 0},
    }));
  }
};

TEST_F(PchEngineTigerLakeBrightnessTest, ReadZero) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  EXPECT_EQ(0.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTigerLakeBrightnessTest, ReadOne) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x9600},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  EXPECT_EQ(1.0, pch_engine.PanelBrightness());
}

TEST_F(PchEngineTigerLakeBrightnessTest, ReadSmallFraction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x004b},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  EXPECT_EQ(0.001953125, pch_engine.PanelBrightness());  // (1 / 2) ^ 9
}

TEST_F(PchEngineTigerLakeBrightnessTest, ReadLargeFraction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x95b5},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  EXPECT_EQ(0.998046875, pch_engine.PanelBrightness());  // 1 - (1 / 2) ^ 9
}

TEST_F(PchEngineTigerLakeBrightnessTest, WriteSmallFraction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x9600},
  }));
  mmio_range_.Expect({.address = kSblcPwmDutyOffset, .value = 0x004b, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelBrightness(0.001953125);
}

TEST_F(PchEngineTigerLakeBrightnessTest, WriteLargeFraction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x0000},
  }));
  mmio_range_.Expect({.address = kSblcPwmDutyOffset, .value = 0x95b5, .write = true});
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelBrightness(0.998046875);
}

TEST_F(PchEngineTigerLakeBrightnessTest, WriteSmallFractionNoChange) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x9600},
      {.address = kSblcPwmDutyOffset, .value = 0x004b},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelBrightness(0.001953125);
}

TEST_F(PchEngineTigerLakeBrightnessTest, WriteMisconfiguredNoChange) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kSblcPwmFreqOffset, .value = 0x0000},
      {.address = kSblcPwmDutyOffset, .value = 0x1111},
  }));
  PchEngine pch_engine(&mmio_buffer_, kDell5420GpuDeviceId);
  pch_engine.SetPanelBrightness(1.0);
}

class PchEnginePanelPowerStateTest : public PchEngineTest {
 protected:
  void SetUp() override {
    PchEngineTest::SetUp();

    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kSChicken1Offset, .value = 1},
        {.address = kRawClkOffset, .value = kKabyLakeStandardRawClock},
        {.address = kPpControlOffset, .value = 0},
        {.address = kPpOnDelays, .value = 0},
        {.address = kPpOffDelays, .value = 0},
        {.address = kPpDivisor, .value = kKabyLakeStandardPpDivisor},
        {.address = kSblcPwmCtl1Offset, .value = 0},
        {.address = kSblcPwmCtl2Offset, .value = 0x0100'0000},
    }));
  }
};

TEST_F(PchEnginePanelPowerStateTest, PoweredDown) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x0000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredDown, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweredUp) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x8000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredUp, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweringUp) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x1000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweringUp, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweringDown) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0xa000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweringDown, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PowerCycleDelay) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x0800'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kWaitingForPowerCycleDelay, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweringUpWaitingForPowerCycleDelay) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x1800'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kWaitingForPowerCycleDelay, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweredDownIgnoringReservedTransition) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x3000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredDown, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweredUpIgnoringReservedTransition) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0xb000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredUp, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PowerCycleDelayIgnoringReservedTransition) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x3800'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kWaitingForPowerCycleDelay, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweredUpIgnoringPowerCycleDelay) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x8800'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredUp, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, PoweredUpIgnoringPowerCycleDelayAndReservedTransition) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0xb800'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_EQ(PchPanelPowerState::kPoweredUp, pch_engine.PanelPowerState());
}

TEST_F(PchEnginePanelPowerStateTest, WaitForPanelPowerStateInstant) {
  mmio_range_.Expect({.address = kPpStatusOffset, .value = 0x8000'0000});

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_TRUE(pch_engine.WaitForPanelPowerState(PchPanelPowerState::kPoweredUp, 30'000));
}

TEST_F(PchEnginePanelPowerStateTest, WaitForPanelPowerStateAfter20Ms) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpStatusOffset, .value = 0x0000'0000},  // Powered down.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x8000'0000},  // Powered up.
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_TRUE(pch_engine.WaitForPanelPowerState(PchPanelPowerState::kPoweredUp, 30'000));
}

TEST_F(PchEnginePanelPowerStateTest, WaitForPanelPowerStateLastChance) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpStatusOffset, .value = 0x0000'0000},  // Powered down.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x8000'0000},  // Powered up.
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_TRUE(pch_engine.WaitForPanelPowerState(PchPanelPowerState::kPoweredUp, 30'000));
}

TEST_F(PchEnginePanelPowerStateTest, WaitForPanelPowerStateTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpStatusOffset, .value = 0x0000'0000},  // Powered down.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_FALSE(pch_engine.WaitForPanelPowerState(PchPanelPowerState::kPoweredUp, 30'000));
}

TEST_F(PchEnginePanelPowerStateTest, WaitForPanelPowerStateTimeoutRounding) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kPpStatusOffset, .value = 0x0000'0000},  // Powered down.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
      {.address = kPpStatusOffset, .value = 0x1000'0000},  // Powering up.
  }));

  PchEngine pch_engine(&mmio_buffer_, kAtlasGpuDeviceId);
  EXPECT_FALSE(pch_engine.WaitForPanelPowerState(PchPanelPowerState::kPoweredUp, 21'000));
}

}  // namespace

}  // namespace i915_tgl
