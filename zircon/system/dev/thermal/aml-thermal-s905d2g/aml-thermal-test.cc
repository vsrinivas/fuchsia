// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstddef>
#include <memory>

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/pwm.h>
#include <zxtest/zxtest.h>

bool operator==(const pwm_config_t& lhs, const pwm_config_t& rhs) {
  return (lhs.polarity == rhs.polarity) && (lhs.period_ns == rhs.period_ns) &&
         (lhs.duty_cycle == rhs.duty_cycle) && (lhs.mode_config_size == rhs.mode_config_size) &&
         !memcmp(lhs.mode_config_buffer, rhs.mode_config_buffer, lhs.mode_config_size);
}

namespace {

constexpr size_t kRegSize = 0x00002000 / sizeof(uint32_t);  // in 32 bits chunks.

// Temperature Sensor
// Copied from sherlock-thermal.cc
constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp_c,
                                                                    uint16_t cpu_opp_big,
                                                                    uint16_t cpu_opp_little,
                                                                    uint16_t gpu_opp) {
  constexpr float kHysteresis = 2.0f;

  return {
      .up_temp_celsius = temp_c + kHysteresis,
      .down_temp_celsius = temp_c - kHysteresis,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp_big,
      .little_cluster_dvfs_opp = cpu_opp_little,
      .gpu_clk_freq_source = gpu_opp,
  };
}

static fuchsia_hardware_thermal_ThermalDeviceInfo fake_thermal_config =
    {
        .active_cooling = false,
        .passive_cooling = true,
        .gpu_throttling = true,
        .num_trip_points = 6,
        .big_little = true,
        .critical_temp_celsius = 102.0f,
        .trip_point_info =
            {
                TripPoint(55.0f, 9, 10, 4), TripPoint(75.0f, 8, 9, 4), TripPoint(80.0f, 7, 8, 3),
                TripPoint(90.0f, 6, 7, 3), TripPoint(95.0f, 5, 6, 3), TripPoint(100.0f, 4, 5, 2),
                TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
            },
        .opps =
            {
                [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                    {
                        .opp =
                            {
                                [0] = {.freq_hz = 100000000, .volt_uv = 751000},
                                [1] = {.freq_hz = 250000000, .volt_uv = 751000},
                                [2] = {.freq_hz = 500000000, .volt_uv = 751000},
                                [3] = {.freq_hz = 667000000, .volt_uv = 751000},
                                [4] = {.freq_hz = 1000000000, .volt_uv = 771000},
                                [5] = {.freq_hz = 1200000000, .volt_uv = 771000},
                                [6] = {.freq_hz = 1398000000, .volt_uv = 791000},
                                [7] = {.freq_hz = 1512000000, .volt_uv = 821000},
                                [8] = {.freq_hz = 1608000000, .volt_uv = 861000},
                                [9] = {.freq_hz = 1704000000, .volt_uv = 891000},
                                [10] = {.freq_hz = 1704000000, .volt_uv = 891000},
                            },
                        .latency = 0,
                        .count = 11,
                    },
                [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] =
                    {
                        .opp =
                            {
                                [0] = {.freq_hz = 100000000, .volt_uv = 731000},
                                [1] = {.freq_hz = 250000000, .volt_uv = 731000},
                                [2] = {.freq_hz = 500000000, .volt_uv = 731000},
                                [3] = {.freq_hz = 667000000, .volt_uv = 731000},
                                [4] = {.freq_hz = 1000000000, .volt_uv = 731000},
                                [5] = {.freq_hz = 1200000000, .volt_uv = 731000},
                                [6] = {.freq_hz = 1398000000, .volt_uv = 761000},
                                [7] = {.freq_hz = 1512000000, .volt_uv = 791000},
                                [8] = {.freq_hz = 1608000000, .volt_uv = 831000},
                                [9] = {.freq_hz = 1704000000, .volt_uv = 861000},
                                [10] = {.freq_hz = 1896000000, .volt_uv = 1011000},
                            },
                        .latency = 0,
                        .count = 11,
                    },
            },
};

static fuchsia_hardware_thermal_ThermalDeviceInfo fake_thermal_config_less =
    {
        .active_cooling = false,
        .passive_cooling = true,
        .gpu_throttling = true,
        .num_trip_points = 2,
        .big_little = true,
        .critical_temp_celsius = 102.0f,
        .trip_point_info =
            {
                TripPoint(55.0f, 9, 10, 4), TripPoint(75.0f, 8, 9, 4),
                TripPoint(-273.15f, 0, 0, 0),  // 0 Kelvin is impossible, marks end of TripPoints
            },
        .opps =
            {
                [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                    {
                        .opp =
                            {
                                [0] = {.freq_hz = 100000000, .volt_uv = 751000},
                                [1] = {.freq_hz = 250000000, .volt_uv = 751000},
                                [2] = {.freq_hz = 500000000, .volt_uv = 751000},
                                [3] = {.freq_hz = 667000000, .volt_uv = 751000},
                                [4] = {.freq_hz = 1000000000, .volt_uv = 771000},
                                [5] = {.freq_hz = 1200000000, .volt_uv = 771000},
                                [6] = {.freq_hz = 1398000000, .volt_uv = 791000},
                                [7] = {.freq_hz = 1512000000, .volt_uv = 821000},
                                [8] = {.freq_hz = 1608000000, .volt_uv = 861000},
                                [9] = {.freq_hz = 1704000000, .volt_uv = 891000},
                                [10] = {.freq_hz = 1704000000, .volt_uv = 891000},
                            },
                        .latency = 0,
                        .count = 11,
                    },
                [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] =
                    {
                        .opp =
                            {
                                [0] = {.freq_hz = 100000000, .volt_uv = 731000},
                                [1] = {.freq_hz = 250000000, .volt_uv = 731000},
                                [2] = {.freq_hz = 500000000, .volt_uv = 731000},
                                [3] = {.freq_hz = 667000000, .volt_uv = 731000},
                                [4] = {.freq_hz = 1000000000, .volt_uv = 731000},
                                [5] = {.freq_hz = 1200000000, .volt_uv = 731000},
                                [6] = {.freq_hz = 1398000000, .volt_uv = 761000},
                                [7] = {.freq_hz = 1512000000, .volt_uv = 791000},
                                [8] = {.freq_hz = 1608000000, .volt_uv = 831000},
                                [9] = {.freq_hz = 1704000000, .volt_uv = 861000},
                                [10] = {.freq_hz = 1896000000, .volt_uv = 1011000},
                            },
                        .latency = 0,
                        .count = 11,
                    },
            },
};

// Voltage Regulator
// Copied from sherlock-thermal.cc
static aml_voltage_table_info_t fake_voltage_table = {
    .voltage_table =
        {
            {1022000, 0},  {1011000, 3}, {1001000, 6}, {991000, 10}, {981000, 13}, {971000, 16},
            {961000, 20},  {951000, 23}, {941000, 26}, {931000, 30}, {921000, 33}, {911000, 36},
            {901000, 40},  {891000, 43}, {881000, 46}, {871000, 50}, {861000, 53}, {851000, 56},
            {841000, 60},  {831000, 63}, {821000, 67}, {811000, 70}, {801000, 73}, {791000, 76},
            {781000, 80},  {771000, 83}, {761000, 86}, {751000, 90}, {741000, 93}, {731000, 96},
            {721000, 100},
        },
};

}  // namespace

namespace thermal {

// Temperature Sensor
class FakeAmlTSensor : public AmlTSensor {
 public:
  static std::unique_ptr<FakeAmlTSensor> Create(ddk::MmioBuffer pll_mmio, ddk::MmioBuffer ao_mmio,
                                                ddk::MmioBuffer hiu_mmio, bool less) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlTSensor>(&ac, std::move(pll_mmio),
                                                         std::move(ao_mmio), std::move(hiu_mmio));
    if (!ac.check()) {
      return nullptr;
    }

    if (less) {
      EXPECT_OK(test->InitSensor(fake_thermal_config_less));
    } else {
      EXPECT_OK(test->InitSensor(fake_thermal_config));
    }
    return test;
  }

  explicit FakeAmlTSensor(ddk::MmioBuffer pll_mmio, ddk::MmioBuffer ao_mmio,
                          ddk::MmioBuffer hiu_mmio)
      : AmlTSensor(std::move(pll_mmio), std::move(ao_mmio), std::move(hiu_mmio)) {}
};

class AmlTSensorTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    pll_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: pll_regs_ alloc failed\n");
      return;
    }
    mock_pll_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, pll_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_pll_mmio_ alloc failed\n");
      return;
    }

    ao_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: ao_regs_ alloc failed\n");
      return;
    }
    mock_ao_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ao_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_ao_mmio_ alloc failed\n");
      return;
    }

    hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: hiu_regs_ alloc failed\n");
      return;
    }
    mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_hiu_mmio_ alloc failed\n");
      return;
    }

    (*mock_ao_mmio_)[0x268].ExpectRead(0x00000000);      // trim_info_
    (*mock_hiu_mmio_)[(0x64 << 2)].ExpectWrite(0x130U);  // set clock
    (*mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)
        .ExpectWrite(0x63B);  // sensor ctl
  }

  void Create(bool less) {
    // InitTripPoints
    if (!less) {
      (*mock_pll_mmio_)[(0x800 + (0x5 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 4, rise
          .ExpectWrite(0x00027E);
      (*mock_pll_mmio_)[(0x800 + (0x7 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 4, fall
          .ExpectWrite(0x000272);
      (*mock_pll_mmio_)[(0x800 + (0x5 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 3, rise
          .ExpectWrite(0x272000);
      (*mock_pll_mmio_)[(0x800 + (0x7 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 3, fall
          .ExpectWrite(0x268000);
      (*mock_pll_mmio_)[(0x800 + (0x4 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 2, rise
          .ExpectWrite(0x00025A);
      (*mock_pll_mmio_)[(0x800 + (0x6 << 2))]
          .ExpectRead(0x00000000)  // set thresholds 2, fall
          .ExpectWrite(0x000251);
    }
    (*mock_pll_mmio_)[(0x800 + (0x4 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 1, rise
        .ExpectWrite(0x250000);
    (*mock_pll_mmio_)[(0x800 + (0x6 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 1, fall
        .ExpectWrite(0x245000);
    (*mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00FF0000);
    (*mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00000000);
    if (!less) {
      (*mock_pll_mmio_)[(0x800 + (0x1 << 2))]
          .ExpectRead(0x00000000)  // enable IRQs
          .ExpectWrite(0x0F008000);
    } else {
      (*mock_pll_mmio_)[(0x800 + (0x1 << 2))]
          .ExpectRead(0x00000000)  // enable IRQs
          .ExpectWrite(0x01008000);
    }

    ddk::MmioBuffer pll_mmio(mock_pll_mmio_->GetMmioBuffer());
    ddk::MmioBuffer ao_mmio(mock_ao_mmio_->GetMmioBuffer());
    ddk::MmioBuffer hiu_mmio(mock_hiu_mmio_->GetMmioBuffer());
    tsensor_ =
        FakeAmlTSensor::Create(std::move(pll_mmio), std::move(ao_mmio), std::move(hiu_mmio), less);
    ASSERT_TRUE(tsensor_ != nullptr);
  }

  void TearDown() override {
    // Verify
    mock_pll_mmio_->VerifyAll();
    mock_ao_mmio_->VerifyAll();
    mock_hiu_mmio_->VerifyAll();
  }

 protected:
  std::unique_ptr<FakeAmlTSensor> tsensor_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> pll_regs_;
  fbl::Array<ddk_mock::MockMmioReg> ao_regs_;
  fbl::Array<ddk_mock::MockMmioReg> hiu_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_pll_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_ao_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_hiu_mmio_;
};

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest0) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x0000);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 0.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest1) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x18A9);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 429496704.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest2) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x32A7);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 0.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest3) {
  Create(false);
  (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x18A9);
  (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x18AA);
  for (int j = 0; j < 0xE; j++) {
    (*mock_pll_mmio_)[(0x800 + (0x10 << 2))].ExpectRead(0x0000);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 429496704.0);
}

TEST_F(AmlTSensorTest, GetStateChangePortTest) {
  Create(false);
  zx_handle_t port;
  EXPECT_OK(tsensor_->GetStateChangePort(&port));
}

TEST_F(AmlTSensorTest, LessTripPointsTest) { Create(true); }

// PWM
class FakeAmlPwm : public AmlPwm {
 public:
  static std::unique_ptr<FakeAmlPwm> Create(ddk::MmioBuffer pwm_mmio, uint32_t period,
                                            uint32_t hwpwm) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlPwm>(&ac, std::move(pwm_mmio));
    if (!ac.check()) {
      return nullptr;
    }

    EXPECT_OK(test->Init(period, hwpwm));
    return test;
  }

  explicit FakeAmlPwm(ddk::MmioBuffer pwm_mmio) : AmlPwm() { MapMmio(std::move(pwm_mmio)); }
};

class AmlPwmTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    pwm_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlPwmTest::SetUp: pwm_regs_ alloc failed\n");
      return;
    }
    mock_pwm_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, pwm_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlPwmTest::SetUp: mock_pwm_mmio_ alloc failed\n");
      return;
    }
  }

  void TearDown() override {
    // Verify
    mock_pwm_mmio_->VerifyAll();
  }

  void Create(uint32_t period, uint32_t hwpwm) {
    ddk::MmioBuffer pwm_mmio(mock_pwm_mmio_->GetMmioBuffer());
    pwm_ = FakeAmlPwm::Create(std::move(pwm_mmio), period, hwpwm);
    ASSERT_TRUE(pwm_ != nullptr);
  }

 protected:
  std::unique_ptr<FakeAmlPwm> pwm_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> pwm_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_pwm_mmio_;

  void TestPwmAConfigure(uint32_t duty_cycle, uint32_t expected, uint32_t expected_misc) {
    (*mock_pwm_mmio_)[(0x0 * 4)].ExpectWrite(expected);
    (*mock_pwm_mmio_)[(0x2 * 4)].ExpectRead(0x00000000).ExpectWrite(expected_misc);
    EXPECT_OK(pwm_->Configure(duty_cycle));
    ASSERT_NO_FATAL_FAILURES(mock_pwm_mmio_->VerifyAll());
  }

  void TestPwmBConfigure(uint32_t duty_cycle, uint32_t expected, uint32_t expected_misc) {
    (*mock_pwm_mmio_)[(0x1 * 4)].ExpectWrite(expected);
    (*mock_pwm_mmio_)[(0x2 * 4)].ExpectRead(0x00000000).ExpectWrite(expected_misc);
    EXPECT_OK(pwm_->Configure(duty_cycle));
    ASSERT_NO_FATAL_FAILURES(mock_pwm_mmio_->VerifyAll());
  }
};

TEST_F(AmlPwmTest, ConfigureFail) {
  Create(10, 0);
  EXPECT_NOT_OK(pwm_->Configure(101));
}

TEST_F(AmlPwmTest, SherlockDvfsSpecPwmA) {
  Create(1250, 0);

  // clang-format off
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(0,   0x0000'001e, 0x1000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(3,   0x0000'001c, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(6,   0x0001'001b, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(10,  0x0002'001a, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(13,  0x0003'0019, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(16,  0x0004'0018, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(20,  0x0005'0017, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(23,  0x0006'0016, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(26,  0x0007'0015, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(30,  0x0008'0014, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(33,  0x0009'0013, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(36,  0x000a'0012, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(40,  0x000b'0011, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(43,  0x000c'0010, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(46,  0x000d'000f, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(50,  0x000e'000e, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(53,  0x000f'000d, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(56,  0x0010'000c, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(60,  0x0011'000b, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(63,  0x0012'000a, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(67,  0x0013'0009, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(70,  0x0014'0008, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(73,  0x0015'0007, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(76,  0x0016'0006, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(80,  0x0017'0005, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(83,  0x0018'0004, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(86,  0x0019'0003, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(90,  0x001a'0002, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(93,  0x001b'0001, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(96,  0x001c'0000, 0x0000'8001));
  ASSERT_NO_FATAL_FAILURES(TestPwmAConfigure(100, 0x001e'0000, 0x1000'8001));
  // clang-format on
}

TEST_F(AmlPwmTest, SherlockDvfsSpecPwmB) {
  Create(1250, 1);

  // clang-format off
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(0,   0x0000'001e, 0x2080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(3,   0x0000'001c, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(6,   0x0001'001b, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(10,  0x0002'001a, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(13,  0x0003'0019, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(16,  0x0004'0018, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(20,  0x0005'0017, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(23,  0x0006'0016, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(26,  0x0007'0015, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(30,  0x0008'0014, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(33,  0x0009'0013, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(36,  0x000a'0012, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(40,  0x000b'0011, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(43,  0x000c'0010, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(46,  0x000d'000f, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(50,  0x000e'000e, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(53,  0x000f'000d, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(56,  0x0010'000c, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(60,  0x0011'000b, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(63,  0x0012'000a, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(67,  0x0013'0009, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(70,  0x0014'0008, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(73,  0x0015'0007, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(76,  0x0016'0006, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(80,  0x0017'0005, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(83,  0x0018'0004, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(86,  0x0019'0003, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(90,  0x001a'0002, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(93,  0x001b'0001, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(96,  0x001c'0000, 0x0080'0002));
  ASSERT_NO_FATAL_FAILURES(TestPwmBConfigure(100, 0x001e'0000, 0x2080'0002));
  // clang-format on
}

// Voltage Regulator
class FakeAmlVoltageRegulator : public AmlVoltageRegulator {
 public:
  static std::unique_ptr<FakeAmlVoltageRegulator> Create(const pwm_protocol_t* pwm_AO_D,
                                                         const pwm_protocol_t* pwm_A,
                                                         uint32_t pid) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlVoltageRegulator>(&ac);
    if (!ac.check()) {
      return nullptr;
    }

    EXPECT_OK(test->Init(pwm_AO_D, pwm_A, pid, &fake_voltage_table));
    return test;
  }

  FakeAmlVoltageRegulator() : AmlVoltageRegulator() {}
};

class AmlVoltageRegulatorTest : public zxtest::Test {
 public:
  void TearDown() override {
    // Verify
    pwm_AO_D_.VerifyAndClear();
    pwm_A_.VerifyAndClear();
  }

  void Create(uint32_t pid) {
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, 1250, 43, &on, sizeof(on)};

    switch (pid) {
      case 4: {  // Sherlock
        pwm_AO_D_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 3;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);

        pwm_A_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 43;
        pwm_A_.ExpectSetConfig(ZX_OK, cfg);
        break;
      }
      case 3: {  // Astro
        pwm_AO_D_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 13;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
        break;
      }
      default:
        zxlogf(ERROR, "AmlVoltageRegulatorTest::Create: unsupported SOC PID %u\n", pid);
        return;
    }

    auto pwm_AO_D = pwm_AO_D_.GetProto();
    auto pwm_A = pwm_A_.GetProto();
    voltage_regulator_ = FakeAmlVoltageRegulator::Create(pwm_AO_D, pwm_A, pid);
    ASSERT_TRUE(voltage_regulator_ != nullptr);
  }

 protected:
  std::unique_ptr<FakeAmlVoltageRegulator> voltage_regulator_;

  // Mmio Regs and Regions
  ddk::MockPwm pwm_AO_D_;
  ddk::MockPwm pwm_A_;
};

TEST_F(AmlVoltageRegulatorTest, SherlockGetVoltageTest) {
  Create(4);
  uint32_t val = voltage_regulator_->GetVoltage(0);
  EXPECT_EQ(val, 891000);
  val = voltage_regulator_->GetVoltage(1);
  EXPECT_EQ(val, 1011000);
}

TEST_F(AmlVoltageRegulatorTest, AstroGetVoltageTest) {
  Create(3);
  uint32_t val = voltage_regulator_->GetVoltage(0);
  EXPECT_EQ(val, 981000);
}

TEST_F(AmlVoltageRegulatorTest, SherlockSetVoltageTest) {
  Create(4);
  // SetBigClusterVoltage(761000)
  aml_pwm::mode_config on = {aml_pwm::ON, {}};
  pwm_config_t cfg = {false, 1250, 53, &on, sizeof(on)};
  pwm_A_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 63;
  pwm_A_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 73;
  pwm_A_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 83;
  pwm_A_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 86;
  pwm_A_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(voltage_regulator_->SetVoltage(0, 761000));
  uint32_t val = voltage_regulator_->GetVoltage(0);
  EXPECT_EQ(val, 761000);

  // SetLittleClusterVoltage(911000)
  cfg.duty_cycle = 13;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 23;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 33;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 36;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(voltage_regulator_->SetVoltage(1, 911000));
  val = voltage_regulator_->GetVoltage(1);
  EXPECT_EQ(val, 911000);
}

TEST_F(AmlVoltageRegulatorTest, AstroSetVoltageTest) {
  Create(3);
  // SetBigClusterVoltage(861000)
  aml_pwm::mode_config on = {aml_pwm::ON, {}};
  pwm_config_t cfg = {false, 1250, 23, &on, sizeof(on)};
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 33;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 43;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 53;
  pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(voltage_regulator_->SetVoltage(0, 861000));
  uint32_t val = voltage_regulator_->GetVoltage(0);
  EXPECT_EQ(val, 861000);
}

// CPU Frequency and Scaling
class FakeAmlCpuFrequency : public AmlCpuFrequency {
 public:
  static std::unique_ptr<FakeAmlCpuFrequency> Create(ddk::MmioBuffer hiu_mmio,
                                                     mmio_buffer_t mock_hiu_internal_mmio,
                                                     uint32_t pid) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlCpuFrequency>(&ac, std::move(hiu_mmio),
                                                              mock_hiu_internal_mmio, pid);
    if (!ac.check()) {
      return nullptr;
    }

    EXPECT_OK(test->Init());
    return test;
  }

  FakeAmlCpuFrequency(ddk::MmioBuffer hiu_mmio, mmio_buffer_t hiu_internal_mmio, uint32_t pid)
      : AmlCpuFrequency(std::move(hiu_mmio), std::move(hiu_internal_mmio), pid) {}
};

class AmlCpuFrequencyTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: hiu_regs_ alloc failed\n");
      return;
    }
    mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: mock_hiu_mmio_ alloc failed\n");
      return;
    }

    hiu_internal_mmio_ = fbl::Array(new (&ac) uint32_t[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: hiu_internal_mmio_ alloc failed\n");
      return;
    }

    mock_hiu_internal_mmio_ = {.vaddr = hiu_internal_mmio_.get(),
                               .offset = 0,
                               .size = kRegSize * sizeof(uint32_t),
                               .vmo = ZX_HANDLE_INVALID};
    InitHiuInternalMmio();
  }

  void TearDown() override {
    // Verify
    mock_hiu_mmio_->VerifyAll();
  }

  void Create(uint32_t pid) {
    switch (pid) {
      case 4: {  // Sherlock
        // Big
        (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);  // WaitForBusyCpu
        (*mock_hiu_mmio_)[520]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use
        // Little
        (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);  // WaitForBusyCpu
        (*mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use
        break;
      }
      case 3: {  // Astro
        // Big
        (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);  // WaitForBusyCpu
        (*mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use
        break;
      }
      default:
        zxlogf(ERROR, "AmlCpuFrequencyTest::Create: unsupported SOC PID %u\n", pid);
        return;
    }

    ddk::MmioBuffer hiu_mmio(mock_hiu_mmio_->GetMmioBuffer());
    cpufreq_scaling_ =
        FakeAmlCpuFrequency::Create(std::move(hiu_mmio), mock_hiu_internal_mmio_, pid);
    ASSERT_TRUE(cpufreq_scaling_ != nullptr);
  }

  void InitHiuInternalMmio() {
    for (uint32_t i = 0; i < kRegSize; i++) {
      hiu_internal_mmio_[i] = (1 << 31);
    }
  }

 protected:
  std::unique_ptr<FakeAmlCpuFrequency> cpufreq_scaling_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> hiu_regs_;
  fbl::Array<uint32_t> hiu_internal_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_hiu_mmio_;
  mmio_buffer_t mock_hiu_internal_mmio_;
};

TEST_F(AmlCpuFrequencyTest, SherlockGetFrequencyTest) {
  Create(4);
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1000000000);
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(1);
  EXPECT_EQ(val, 1000000000);
}

TEST_F(AmlCpuFrequencyTest, AstroGetFrequencyTest) {
  Create(3);
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1000000000);
}

TEST_F(AmlCpuFrequencyTest, SherlockSetFrequencyTest0) {
  Create(4);
  // Big
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectWrite(0x00350400);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 250000000));
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 250000000);

  // Little
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00350400);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(1, 250000000));
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(1);
  EXPECT_EQ(val, 250000000);
}

TEST_F(AmlCpuFrequencyTest, SherlockSetFrequencyTest1) {
  Create(4);
  // Big
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 1536000000));
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1536000000);

  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectWrite(0x00010400);
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 1494000000));
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1494000000);

  // Little
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(1, 1200000000));
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(1);
  EXPECT_EQ(val, 1200000000);

  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00010400);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(1, 1398000000));
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(1);
  EXPECT_EQ(val, 1398000000);
}

TEST_F(AmlCpuFrequencyTest, AstroSetFrequencyTest0) {
  Create(3);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00350400);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 250000000));
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 250000000);
}

TEST_F(AmlCpuFrequencyTest, AstroSetFrequencyTest1) {
  Create(3);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 1536000000));
  InitHiuInternalMmio();
  uint32_t val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1536000000);

  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x10400);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
  (*mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);
  InitHiuInternalMmio();
  EXPECT_OK(cpufreq_scaling_->SetFrequency(0, 1494000000));
  InitHiuInternalMmio();
  val = cpufreq_scaling_->GetFrequency(0);
  EXPECT_EQ(val, 1494000000);
}

// Thermal
class FakeAmlThermal : public AmlThermal {
 public:
  static std::unique_ptr<FakeAmlThermal> Create(
      ddk::MmioBuffer tsensor_pll_mmio, ddk::MmioBuffer tsensor_ao_mmio,
      ddk::MmioBuffer tsensor_hiu_mmio, const pwm_protocol_t* pwm_AO_D, const pwm_protocol_t* pwm_A,
      ddk::MmioBuffer cpufreq_scaling_hiu_mmio,
      mmio_buffer_t cpufreq_scaling_mock_hiu_internal_mmio, uint32_t pid) {
    fbl::AllocChecker ac;

    // Temperature Sensor
    auto tsensor = fbl::make_unique_checked<AmlTSensor>(
        &ac, std::move(tsensor_pll_mmio), std::move(tsensor_ao_mmio), std::move(tsensor_hiu_mmio));
    if (!ac.check()) {
      return nullptr;
    }
    EXPECT_OK(tsensor->InitSensor(fake_thermal_config));

    // Voltage Regulator
    zx_status_t status = ZX_OK;
    auto voltage_regulator = fbl::make_unique_checked<AmlVoltageRegulator>(&ac);
    if (!ac.check() || (status != ZX_OK)) {
      return nullptr;
    }
    EXPECT_OK(voltage_regulator->Init(pwm_AO_D, pwm_A, pid, &fake_voltage_table));

    // CPU Frequency and Scaling
    auto cpufreq_scaling = fbl::make_unique_checked<AmlCpuFrequency>(
        &ac, std::move(cpufreq_scaling_hiu_mmio), std::move(cpufreq_scaling_mock_hiu_internal_mmio),
        pid);
    if (!ac.check()) {
      return nullptr;
    }
    EXPECT_OK(cpufreq_scaling->Init());

    auto test = fbl::make_unique_checked<FakeAmlThermal>(
        &ac, std::move(tsensor), std::move(voltage_regulator), std::move(cpufreq_scaling));
    if (!ac.check()) {
      return nullptr;
    }

    // SetTarget
    if (pid == 4) {
      // Sherlock
      uint32_t big_opp_idx = fake_thermal_config.trip_point_info[0].big_cluster_dvfs_opp;
      EXPECT_OK(test->SetTarget(big_opp_idx,
                                fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN));

      uint32_t little_opp_idx = fake_thermal_config.trip_point_info[0].little_cluster_dvfs_opp;
      EXPECT_OK(test->SetTarget(little_opp_idx,
                                fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN));
    } else if (pid == 3) {
      // Astro
      uint32_t opp_idx = fake_thermal_config.trip_point_info[0].big_cluster_dvfs_opp;
      EXPECT_OK(
          test->SetTarget(opp_idx, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN));
    }

    return test;
  }

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  FakeAmlThermal(std::unique_ptr<thermal::AmlTSensor> tsensor,
                 std::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator,
                 std::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling)
      : AmlThermal(nullptr, std::move(tsensor), std::move(voltage_regulator),
                   std::move(cpufreq_scaling), std::move(fake_thermal_config)) {}
};

class AmlThermalTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    // Temperature Sensor
    tsensor_pll_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_pll_regs_ alloc failed\n");
      return;
    }
    tsensor_mock_pll_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_pll_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_pll_mmio_ alloc failed\n");
      return;
    }
    tsensor_ao_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_ao_regs_ alloc failed\n");
      return;
    }
    tsensor_mock_ao_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_ao_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_ao_mmio_ alloc failed\n");
      return;
    }
    tsensor_hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_hiu_regs_ alloc failed\n");
      return;
    }
    tsensor_mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_hiu_mmio_ alloc failed\n");
      return;
    }
    (*tsensor_mock_ao_mmio_)[0x268].ExpectRead(0x00000000);      // trim_info_
    (*tsensor_mock_hiu_mmio_)[(0x64 << 2)].ExpectWrite(0x130U);  // set clock
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)
        .ExpectWrite(0x63B);  // sensor ctl
    // InitTripPoints
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x5 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 4, rise
        .ExpectWrite(0x00027E);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x7 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 4, fall
        .ExpectWrite(0x000272);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x5 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 3, rise
        .ExpectWrite(0x272000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x7 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 3, fall
        .ExpectWrite(0x268000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x4 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 2, rise
        .ExpectWrite(0x00025A);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x6 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 2, fall
        .ExpectWrite(0x000251);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x4 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 1, rise
        .ExpectWrite(0x250000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x6 << 2))]
        .ExpectRead(0x00000000)  // set thresholds 1, fall
        .ExpectWrite(0x245000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00FF0000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00000000);
    (*tsensor_mock_pll_mmio_)[(0x800 + (0x1 << 2))]
        .ExpectRead(0x00000000)  // enable IRQs
        .ExpectWrite(0x0F008000);

    // CPU Frequency and Scaling
    cpufreq_scaling_hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_hiu_regs_ alloc failed\n");
      return;
    }
    cpufreq_scaling_mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, cpufreq_scaling_hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_mock_hiu_mmio_ alloc failed\n");
      return;
    }
    cpufreq_scaling_hiu_internal_mmio_ = fbl::Array(new (&ac) uint32_t[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_hiu_internal_mmio_ alloc failed\n");
      return;
    }

    cpufreq_scaling_mock_hiu_internal_mmio_ = {.vaddr = cpufreq_scaling_hiu_internal_mmio_.get(),
                                               .offset = 0,
                                               .size = kRegSize * sizeof(uint32_t),
                                               .vmo = ZX_HANDLE_INVALID};
    InitHiuInternalMmio();
  }

  void TearDown() override {
    // Verify
    tsensor_mock_pll_mmio_->VerifyAll();
    tsensor_mock_ao_mmio_->VerifyAll();
    tsensor_mock_hiu_mmio_->VerifyAll();
    pwm_AO_D_.VerifyAndClear();
    pwm_A_.VerifyAndClear();
    cpufreq_scaling_mock_hiu_mmio_->VerifyAll();

    // Tear down
    thermal_device_->DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));
    thermal_device_ = nullptr;
  }

  void Create(uint32_t pid) {
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, 1250, 43, &on, sizeof(on)};
    switch (pid) {
      case 4: {  // Sherlock
        // Voltage Regulator
        pwm_AO_D_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 3;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);

        pwm_A_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 43;
        pwm_A_.ExpectSetConfig(ZX_OK, cfg);

        // CPU Frequency and Scaling
        // Big
        (*cpufreq_scaling_mock_hiu_mmio_)[520]
            .ExpectRead(0x00000000)
            .ExpectRead(0x00000000);  // WaitForBusyCpu
        (*cpufreq_scaling_mock_hiu_mmio_)[520]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use
        // Little
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectRead(0x00000000);  // WaitForBusyCpu
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use

        // SetTarget
        (*cpufreq_scaling_mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectRead(0x00000000);
        (*cpufreq_scaling_mock_hiu_mmio_)[520].ExpectRead(0x00000000).ExpectWrite(0x00000800);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);

        break;
      }
      case 3: {  // Astro
        // Voltage Regulator
        pwm_AO_D_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 13;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);

        // CPU Frequency and Scaling
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectRead(0x00000000);  // WaitForBusyCpu
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use

        // SetTarget
        cfg.duty_cycle = 23;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
        cfg.duty_cycle = 33;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
        cfg.duty_cycle = 43;
        pwm_AO_D_.ExpectSetConfig(ZX_OK, cfg);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);
        break;
      }
      default:
        zxlogf(ERROR, "AmlThermalTest::Create: unsupported SOC PID %u\n", pid);
        return;
    }

    ddk::MmioBuffer tsensor_pll_mmio(tsensor_mock_pll_mmio_->GetMmioBuffer());
    ddk::MmioBuffer tsensor_ao_mmio(tsensor_mock_ao_mmio_->GetMmioBuffer());
    ddk::MmioBuffer tsensor_hiu_mmio(tsensor_mock_hiu_mmio_->GetMmioBuffer());
    auto pwm_AO_D = pwm_AO_D_.GetProto();
    auto pwm_A = pwm_A_.GetProto();
    ddk::MmioBuffer cpufreq_scaling_hiu_mmio(cpufreq_scaling_mock_hiu_mmio_->GetMmioBuffer());
    thermal_device_ = FakeAmlThermal::Create(
        std::move(tsensor_pll_mmio), std::move(tsensor_ao_mmio), std::move(tsensor_hiu_mmio),
        pwm_AO_D, pwm_A, std::move(cpufreq_scaling_hiu_mmio),
        cpufreq_scaling_mock_hiu_internal_mmio_, pid);
    ASSERT_TRUE(thermal_device_ != nullptr);
  }

  void InitHiuInternalMmio() {
    for (uint32_t i = 0; i < kRegSize; i++) {
      cpufreq_scaling_hiu_internal_mmio_[i] = (1 << 31);
    }
  }

 protected:
  std::unique_ptr<FakeAmlThermal> thermal_device_;

  // Temperature Sensor
  fbl::Array<ddk_mock::MockMmioReg> tsensor_pll_regs_;
  fbl::Array<ddk_mock::MockMmioReg> tsensor_ao_regs_;
  fbl::Array<ddk_mock::MockMmioReg> tsensor_hiu_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_pll_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_ao_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_hiu_mmio_;

  // Voltage Regulator
  ddk::MockPwm pwm_AO_D_;
  ddk::MockPwm pwm_A_;

  // CPU Frequency and Scaling
  fbl::Array<ddk_mock::MockMmioReg> cpufreq_scaling_hiu_regs_;
  fbl::Array<uint32_t> cpufreq_scaling_hiu_internal_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> cpufreq_scaling_mock_hiu_mmio_;
  mmio_buffer_t cpufreq_scaling_mock_hiu_internal_mmio_;
};

TEST_F(AmlThermalTest, SherlockInitTest) {
  Create(4);
  ASSERT_TRUE(true);
}

TEST_F(AmlThermalTest, AstroInitTest) {
  Create(3);
  ASSERT_TRUE(true);
}

}  // namespace thermal
