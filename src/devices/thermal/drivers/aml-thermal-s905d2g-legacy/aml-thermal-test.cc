// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <fuchsia/hardware/pwm/cpp/banjo-mock.h>
#include <lib/ddk/device.h>
#include <lib/ddk/mmio-buffer.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstddef>
#include <memory>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
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
                                                                    float hysteresis_c,
                                                                    uint16_t cpu_opp_big,
                                                                    uint16_t cpu_opp_little,
                                                                    uint16_t gpu_opp) {
  return {
      .up_temp_celsius = temp_c + hysteresis_c,
      .down_temp_celsius = temp_c - hysteresis_c,
      .fan_level = 0,
      .big_cluster_dvfs_opp = cpu_opp_big,
      .little_cluster_dvfs_opp = cpu_opp_little,
      .gpu_clk_freq_source = gpu_opp,
  };
}

constexpr fuchsia_hardware_thermal_ThermalDeviceInfo
    sherlock_thermal_config =
        {
            .active_cooling = false,
            .passive_cooling = true,
            .gpu_throttling = true,
            .num_trip_points = 6,
            .big_little = true,
            .critical_temp_celsius = 102.0f,
            .trip_point_info =
                {
                    TripPoint(55.0f, 2.0f, 9, 10, 4),
                    TripPoint(75.0f, 2.0f, 8, 9, 4),
                    TripPoint(80.0f, 2.0f, 7, 8, 3),
                    TripPoint(90.0f, 2.0f, 6, 7, 3),
                    TripPoint(95.0f, 2.0f, 5, 6, 3),
                    TripPoint(100.0f, 2.0f, 4, 5, 2),
                    // 0 Kelvin is impossible, marks end of TripPoints
                    TripPoint(-273.15f, 2.0f, 0, 0, 0),
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

constexpr fuchsia_hardware_thermal_ThermalDeviceInfo astro_thermal_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 7,
    .big_little = false,
    .critical_temp_celsius = 102.0f,
    .trip_point_info =
        {
            TripPoint(0.0f, 2.0f, 10, 0, 5),
            TripPoint(75.0f, 2.0f, 9, 0, 4),
            TripPoint(80.0f, 2.0f, 8, 0, 3),
            TripPoint(85.0f, 2.0f, 7, 0, 3),
            TripPoint(90.0f, 2.0f, 6, 0, 2),
            TripPoint(95.0f, 2.0f, 5, 0, 1),
            TripPoint(100.0f, 2.0f, 4, 0, 0),
            // 0 Kelvin is impossible, marks end of TripPoints
            TripPoint(-273.15f, 2.0f, 0, 0, 0),
        },
    .opps =
        {
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                {
                    .opp =
                        {
                            [0] = {.freq_hz = 100'000'000, .volt_uv = 731'000},
                            [1] = {.freq_hz = 250'000'000, .volt_uv = 731'000},
                            [2] = {.freq_hz = 500'000'000, .volt_uv = 731'000},
                            [3] = {.freq_hz = 667'000'000, .volt_uv = 731'000},
                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 731'000},
                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 731'000},
                            [6] = {.freq_hz = 1'398'000'000, .volt_uv = 761'000},
                            [7] = {.freq_hz = 1'512'000'000, .volt_uv = 791'000},
                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 831'000},
                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 861'000},
                            [10] = {.freq_hz = 1'896'000'000, .volt_uv = 981'000},
                        },
                    .latency = 0,
                    .count = 11,
                },
        },
};

constexpr fuchsia_hardware_thermal_ThermalDeviceInfo nelson_thermal_config = {
    .active_cooling = false,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 5,
    .big_little = false,
    .critical_temp_celsius = 110.0f,
    .trip_point_info =
        {
            TripPoint(0.0f, 5.0f, 11, 0, 5),
            TripPoint(60.0f, 5.0f, 9, 0, 4),
            TripPoint(75.0f, 5.0f, 8, 0, 3),
            TripPoint(80.0f, 5.0f, 7, 0, 2),
            TripPoint(110.0f, 1.0f, 0, 0, 0),
            // 0 Kelvin is impossible, marks end of TripPoints
            TripPoint(-273.15f, 2.0f, 0, 0, 0),
        },
    .opps =
        {
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] =
                {
                    .opp =
                        {
                            [0] = {.freq_hz = 100'000'000, .volt_uv = 760'000},
                            [1] = {.freq_hz = 250'000'000, .volt_uv = 760'000},
                            [2] = {.freq_hz = 500'000'000, .volt_uv = 760'000},
                            [3] = {.freq_hz = 667'000'000, .volt_uv = 780'000},
                            [4] = {.freq_hz = 1'000'000'000, .volt_uv = 800'000},
                            [5] = {.freq_hz = 1'200'000'000, .volt_uv = 810'000},
                            [6] = {.freq_hz = 1'404'000'000, .volt_uv = 820'000},
                            [7] = {.freq_hz = 1'512'000'000, .volt_uv = 830'000},
                            [8] = {.freq_hz = 1'608'000'000, .volt_uv = 860'000},
                            [9] = {.freq_hz = 1'704'000'000, .volt_uv = 900'000},
                            [10] = {.freq_hz = 1'800'000'000, .volt_uv = 940'000},
                            [11] = {.freq_hz = 1'908'000'000, .volt_uv = 970'000},
                        },
                    .latency = 0,
                    .count = 12,
                },
        },
};

// Voltage Regulator
// Copied from sherlock-thermal.cc
static aml_thermal_info_t fake_thermal_info = {
    .voltage_table =
        {
            {1022000, 0},  {1011000, 3}, {1001000, 6}, {991000, 10}, {981000, 13}, {971000, 16},
            {961000, 20},  {951000, 23}, {941000, 26}, {931000, 30}, {921000, 33}, {911000, 36},
            {901000, 40},  {891000, 43}, {881000, 46}, {871000, 50}, {861000, 53}, {851000, 56},
            {841000, 60},  {831000, 63}, {821000, 67}, {811000, 70}, {801000, 73}, {791000, 76},
            {781000, 80},  {771000, 83}, {761000, 86}, {751000, 90}, {741000, 93}, {731000, 96},
            {721000, 100},
        },
    .initial_cluster_frequencies =
        {
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'000'000'000,
            [fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN] = 1'200'000'000,
        },
    .voltage_pwm_period_ns = 1250,
    .opps = {},
    .cluster_id_map = {},
};

constexpr aml_thermal_info_t nelson_thermal_info = {
    .voltage_table =
        {
            {1'050'000, 0},  {1'040'000, 3}, {1'030'000, 6}, {1'020'000, 8}, {1'010'000, 11},
            {1'000'000, 14}, {990'000, 17},  {980'000, 20},  {970'000, 23},  {960'000, 26},
            {950'000, 29},   {940'000, 31},  {930'000, 34},  {920'000, 37},  {910'000, 40},
            {900'000, 43},   {890'000, 45},  {880'000, 48},  {870'000, 51},  {860'000, 54},
            {850'000, 56},   {840'000, 59},  {830'000, 62},  {820'000, 65},  {810'000, 68},
            {800'000, 70},   {790'000, 73},  {780'000, 76},  {770'000, 79},  {760'000, 81},
            {750'000, 84},   {740'000, 87},  {730'000, 89},  {720'000, 92},  {710'000, 95},
            {700'000, 98},   {690'000, 100},
        },
    .initial_cluster_frequencies =
        {
            [fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN] = 1'000'000'000,
        },
    .voltage_pwm_period_ns = 1500,
    .opps = {},
    .cluster_id_map = {},
};

}  // namespace

namespace thermal {

// Temperature Sensor
class FakeAmlTSensor : public AmlTSensor {
 public:
  static std::unique_ptr<FakeAmlTSensor> Create(ddk::MmioBuffer pll_mmio, ddk::MmioBuffer trim_mmio,
                                                ddk::MmioBuffer hiu_mmio, bool less) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlTSensor>(&ac, std::move(pll_mmio),
                                                         std::move(trim_mmio), std::move(hiu_mmio));
    if (!ac.check()) {
      return nullptr;
    }

    auto config = sherlock_thermal_config;
    if (less) {
      config.num_trip_points = 2;
      config.trip_point_info[2].up_temp_celsius = -273.15f + 2.0f;
    }

    EXPECT_OK(test->InitSensor(config));
    return test;
  }

  explicit FakeAmlTSensor(ddk::MmioBuffer pll_mmio, ddk::MmioBuffer trim_mmio,
                          ddk::MmioBuffer hiu_mmio)
      : AmlTSensor(std::move(pll_mmio), std::move(trim_mmio), std::move(hiu_mmio)) {}
};

class AmlTSensorTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    pll_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: pll_regs_ alloc failed");
      return;
    }
    mock_pll_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, pll_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_pll_mmio_ alloc failed");
      return;
    }

    trim_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: trim_regs_ alloc failed");
      return;
    }
    mock_trim_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, trim_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_trim_mmio_ alloc failed");
      return;
    }

    hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: hiu_regs_ alloc failed");
      return;
    }
    mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_hiu_mmio_ alloc failed");
      return;
    }

    (*mock_trim_mmio_)[0].ExpectRead(0x00000000);                             // trim_info_
    (*mock_hiu_mmio_)[(0x64 << 2)].ExpectWrite(0x130U);                       // set clock
    (*mock_pll_mmio_)[(0x1 << 2)].ExpectRead(0x00000000).ExpectWrite(0x63B);  // sensor ctl
  }

  void Create(bool less) {
    // InitTripPoints
    if (!less) {
      (*mock_pll_mmio_)[(0x5 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 4, rise
          .ExpectWrite(0x00027E);
      (*mock_pll_mmio_)[(0x7 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 4, fall
          .ExpectWrite(0x000272);
      (*mock_pll_mmio_)[(0x5 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 3, rise
          .ExpectWrite(0x272000);
      (*mock_pll_mmio_)[(0x7 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 3, fall
          .ExpectWrite(0x268000);
      (*mock_pll_mmio_)[(0x4 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 2, rise
          .ExpectWrite(0x00025A);
      (*mock_pll_mmio_)[(0x6 << 2)]
          .ExpectRead(0x00000000)  // set thresholds 2, fall
          .ExpectWrite(0x000251);
    }
    (*mock_pll_mmio_)[(0x4 << 2)]
        .ExpectRead(0x00000000)  // set thresholds 1, rise
        .ExpectWrite(0x250000);
    (*mock_pll_mmio_)[(0x6 << 2)]
        .ExpectRead(0x00000000)  // set thresholds 1, fall
        .ExpectWrite(0x245000);
    (*mock_pll_mmio_)[(0x1 << 2)]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00FF0000);
    (*mock_pll_mmio_)[(0x1 << 2)]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00000000);
    if (!less) {
      (*mock_pll_mmio_)[(0x1 << 2)]
          .ExpectRead(0x00000000)  // enable IRQs
          .ExpectWrite(0x0F008000);
    } else {
      (*mock_pll_mmio_)[(0x1 << 2)]
          .ExpectRead(0x00000000)  // enable IRQs
          .ExpectWrite(0x01008000);
    }

    // Enable SoC reset at 102.0f
    (*mock_pll_mmio_)[(0x2 << 2)].ExpectRead(0x0);
    (*mock_pll_mmio_)[(0x2 << 2)].ExpectWrite(0xc0ff2880);

    ddk::MmioBuffer pll_mmio(mock_pll_mmio_->GetMmioBuffer());
    ddk::MmioBuffer trim_mmio(mock_trim_mmio_->GetMmioBuffer());
    ddk::MmioBuffer hiu_mmio(mock_hiu_mmio_->GetMmioBuffer());
    tsensor_ = FakeAmlTSensor::Create(std::move(pll_mmio), std::move(trim_mmio),
                                      std::move(hiu_mmio), less);
    ASSERT_TRUE(tsensor_ != nullptr);
  }

  void TearDown() override {
    // Verify
    mock_pll_mmio_->VerifyAll();
    mock_trim_mmio_->VerifyAll();
    mock_hiu_mmio_->VerifyAll();
  }

 protected:
  std::unique_ptr<FakeAmlTSensor> tsensor_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> pll_regs_;
  fbl::Array<ddk_mock::MockMmioReg> trim_regs_;
  fbl::Array<ddk_mock::MockMmioReg> hiu_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_pll_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_trim_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_hiu_mmio_;
};

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest0) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x0000);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 0.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest1) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x18A9);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 429496704.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest2) {
  Create(false);
  for (int j = 0; j < 0x10; j++) {
    (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x32A7);
  }

  float val = tsensor_->ReadTemperatureCelsius();
  EXPECT_EQ(val, 0.0);
}

TEST_F(AmlTSensorTest, ReadTemperatureCelsiusTest3) {
  Create(false);
  (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x18A9);
  (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x18AA);
  for (int j = 0; j < 0xE; j++) {
    (*mock_pll_mmio_)[(0x10 << 2)].ExpectRead(0x0000);
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

// Voltage Regulator
class FakeAmlVoltageRegulator : public AmlVoltageRegulator {
 public:
  static std::unique_ptr<FakeAmlVoltageRegulator> Create(const pwm_protocol_t* big_cluster_pwm,
                                                         const pwm_protocol_t* little_cluster_pwm,
                                                         uint32_t pid) {
    fbl::AllocChecker ac;

    auto test = fbl::make_unique_checked<FakeAmlVoltageRegulator>(&ac);
    if (!ac.check()) {
      return nullptr;
    }

    const auto& config = (pid == 4 ? sherlock_thermal_config : astro_thermal_config);
    EXPECT_OK(test->Init(big_cluster_pwm, little_cluster_pwm, config, &fake_thermal_info));
    return test;
  }

  FakeAmlVoltageRegulator() : AmlVoltageRegulator() {}
};

class AmlVoltageRegulatorTest : public zxtest::Test {
 public:
  void TearDown() override {
    // Verify
    big_cluster_pwm_.VerifyAndClear();
    little_cluster_pwm_.VerifyAndClear();
  }

  void Create(uint32_t pid) {
    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, 1250, 43, reinterpret_cast<uint8_t*>(&on), sizeof(on)};

    switch (pid) {
      case 4: {  // Sherlock
        big_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 43;
        big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);

        little_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 3;
        little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
        break;
      }
      case 3: {  // Astro
        big_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 13;
        big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
        break;
      }
      default:
        zxlogf(ERROR, "AmlVoltageRegulatorTest::Create: unsupported SOC PID %u", pid);
        return;
    }

    auto big_cluster_pwm = big_cluster_pwm_.GetProto();
    auto little_cluster_pwm = little_cluster_pwm_.GetProto();
    voltage_regulator_ = FakeAmlVoltageRegulator::Create(big_cluster_pwm, little_cluster_pwm, pid);
    ASSERT_TRUE(voltage_regulator_ != nullptr);
  }

 protected:
  std::unique_ptr<FakeAmlVoltageRegulator> voltage_regulator_;

  // Mmio Regs and Regions
  ddk::MockPwm big_cluster_pwm_;
  ddk::MockPwm little_cluster_pwm_;
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
  pwm_config_t cfg = {false, 1250, 53, reinterpret_cast<uint8_t*>(&on), sizeof(on)};
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 63;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 73;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 83;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 86;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(voltage_regulator_->SetVoltage(0, 761000));
  uint32_t val = voltage_regulator_->GetVoltage(0);
  EXPECT_EQ(val, 761000);

  // SetLittleClusterVoltage(911000)
  cfg.duty_cycle = 13;
  little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 23;
  little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 33;
  little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 36;
  little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  EXPECT_OK(voltage_regulator_->SetVoltage(1, 911000));
  val = voltage_regulator_->GetVoltage(1);
  EXPECT_EQ(val, 911000);
}

TEST_F(AmlVoltageRegulatorTest, AstroSetVoltageTest) {
  Create(3);
  // SetBigClusterVoltage(861000)
  aml_pwm::mode_config on = {aml_pwm::ON, {}};
  pwm_config_t cfg = {false, 1250, 23, reinterpret_cast<uint8_t*>(&on), sizeof(on)};
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 33;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 43;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
  cfg.duty_cycle = 53;
  big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);
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
    const auto& config = (pid == 4 ? sherlock_thermal_config : astro_thermal_config);

    fbl::AllocChecker ac;
    auto test = fbl::make_unique_checked<FakeAmlCpuFrequency>(
        &ac, std::move(hiu_mmio), mock_hiu_internal_mmio, config, fake_thermal_info);
    if (!ac.check()) {
      return nullptr;
    }

    EXPECT_OK(test->Init());
    return test;
  }

  FakeAmlCpuFrequency(ddk::MmioBuffer hiu_mmio, mmio_buffer_t hiu_internal_mmio,
                      const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config,
                      const aml_thermal_info_t& thermal_info)
      : AmlCpuFrequency(std::move(hiu_mmio), std::move(hiu_internal_mmio), thermal_config,
                        thermal_info) {}
};

class AmlCpuFrequencyTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: hiu_regs_ alloc failed");
      return;
    }
    mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: mock_hiu_mmio_ alloc failed");
      return;
    }

    hiu_internal_mmio_ = fbl::Array(new (&ac) uint32_t[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlCpuFrequencyTest::SetUp: hiu_internal_mmio_ alloc failed");
      return;
    }

    mock_hiu_internal_mmio_ = {.vaddr = FakeMmioPtr(hiu_internal_mmio_.get()),
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
        zxlogf(ERROR, "AmlCpuFrequencyTest::Create: unsupported SOC PID %u", pid);
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
      ddk::MmioBuffer tsensor_pll_mmio, ddk::MmioBuffer tsensor_trim_mmio,
      ddk::MmioBuffer tsensor_hiu_mmio, const pwm_protocol_t* big_cluster_pwm,
      const pwm_protocol_t* little_cluster_pwm, ddk::MmioBuffer cpufreq_scaling_hiu_mmio,
      mmio_buffer_t cpufreq_scaling_mock_hiu_internal_mmio, uint32_t pid) {
    fbl::AllocChecker ac;

    const auto& config = (pid == 4 ? sherlock_thermal_config
                                   : (pid == 5 ? nelson_thermal_config : astro_thermal_config));
    const auto& info = (pid == 5 ? nelson_thermal_info : fake_thermal_info);

    // Temperature Sensor
    auto tsensor = fbl::make_unique_checked<AmlTSensor>(&ac, std::move(tsensor_pll_mmio),
                                                        std::move(tsensor_trim_mmio),
                                                        std::move(tsensor_hiu_mmio));
    if (!ac.check()) {
      return nullptr;
    }
    EXPECT_OK(tsensor->InitSensor(config));

    // Voltage Regulator
    zx_status_t status = ZX_OK;
    auto voltage_regulator = fbl::make_unique_checked<AmlVoltageRegulator>(&ac);
    if (!ac.check() || (status != ZX_OK)) {
      return nullptr;
    }
    EXPECT_OK(voltage_regulator->Init(big_cluster_pwm, little_cluster_pwm, config, &info));

    // CPU Frequency and Scaling
    auto cpufreq_scaling = fbl::make_unique_checked<AmlCpuFrequency>(
        &ac, std::move(cpufreq_scaling_hiu_mmio), std::move(cpufreq_scaling_mock_hiu_internal_mmio),
        config, fake_thermal_info);
    if (!ac.check()) {
      return nullptr;
    }
    EXPECT_OK(cpufreq_scaling->Init());

    auto test = fbl::make_unique_checked<FakeAmlThermal>(
        &ac, std::move(tsensor), std::move(voltage_regulator), std::move(cpufreq_scaling), config);
    if (!ac.check()) {
      return nullptr;
    }

    // SetTarget
    EXPECT_OK(test->SetTarget(config.trip_point_info[0].big_cluster_dvfs_opp,
                              fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN));
    if (config.big_little) {
      EXPECT_OK(test->SetTarget(config.trip_point_info[0].little_cluster_dvfs_opp,
                                fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN));
    }

    return test;
  }

  void DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  FakeAmlThermal(std::unique_ptr<thermal::AmlTSensor> tsensor,
                 std::unique_ptr<thermal::AmlVoltageRegulator> voltage_regulator,
                 std::unique_ptr<thermal::AmlCpuFrequency> cpufreq_scaling,
                 const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_config)
      : AmlThermal(nullptr, std::move(tsensor), std::move(voltage_regulator),
                   std::move(cpufreq_scaling), thermal_config) {}
};

class AmlThermalTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    // Temperature Sensor
    tsensor_pll_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_pll_regs_ alloc failed");
      return;
    }
    tsensor_mock_pll_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_pll_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_pll_mmio_ alloc failed");
      return;
    }
    tsensor_trim_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_trim_regs_ alloc failed");
      return;
    }
    tsensor_mock_trim_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_trim_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_trim_mmio_ alloc failed");
      return;
    }
    tsensor_hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: tsensor_hiu_regs_ alloc failed");
      return;
    }
    tsensor_mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, tsensor_hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: mock_hiu_mmio_ alloc failed");
      return;
    }
    (*tsensor_mock_trim_mmio_)[0].ExpectRead(0x00000000);                             // trim_info_
    (*tsensor_mock_hiu_mmio_)[(0x64 << 2)].ExpectWrite(0x130U);                       // set clock
    (*tsensor_mock_pll_mmio_)[(0x1 << 2)].ExpectRead(0x00000000).ExpectWrite(0x63B);  // sensor ctl
    (*tsensor_mock_pll_mmio_)[(0x1 << 2)]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00FF0000);
    (*tsensor_mock_pll_mmio_)[(0x1 << 2)]
        .ExpectRead(0x00000000)  // clear IRQs
        .ExpectWrite(0x00000000);
    (*tsensor_mock_pll_mmio_)[(0x1 << 2)]
        .ExpectRead(0x00000000)  // enable IRQs
        .ExpectWrite(0x0F008000);

    // CPU Frequency and Scaling
    cpufreq_scaling_hiu_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_hiu_regs_ alloc failed");
      return;
    }
    cpufreq_scaling_mock_hiu_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, cpufreq_scaling_hiu_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_mock_hiu_mmio_ alloc failed");
      return;
    }
    cpufreq_scaling_hiu_internal_mmio_ = fbl::Array(new (&ac) uint32_t[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlThermalTest::SetUp: cpufreq_scaling_hiu_internal_mmio_ alloc failed");
      return;
    }

    cpufreq_scaling_mock_hiu_internal_mmio_ = {
        .vaddr = FakeMmioPtr(cpufreq_scaling_hiu_internal_mmio_.get()),
        .offset = 0,
        .size = kRegSize * sizeof(uint32_t),
        .vmo = ZX_HANDLE_INVALID};
    InitHiuInternalMmio();
  }

  void TearDown() override {
    // Verify
    tsensor_mock_pll_mmio_->VerifyAll();
    tsensor_mock_trim_mmio_->VerifyAll();
    tsensor_mock_hiu_mmio_->VerifyAll();
    big_cluster_pwm_.VerifyAndClear();
    little_cluster_pwm_.VerifyAndClear();
    cpufreq_scaling_mock_hiu_mmio_->VerifyAll();

    // Tear down
    thermal_device_->DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
    thermal_device_ = nullptr;
  }

  void Create(uint32_t pid) {
    ddk_mock::MockMmioRegRegion& tsensor_mmio = *tsensor_mock_pll_mmio_;

    aml_pwm::mode_config on = {aml_pwm::ON, {}};
    pwm_config_t cfg = {false, 1250, 43, reinterpret_cast<uint8_t*>(&on), sizeof(on)};
    switch (pid) {
      case 4: {  // Sherlock
        // Voltage Regulator
        big_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 43;
        big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);

        little_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 3;
        little_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);

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

        // InitTripPoints
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x00027E);  // set thresholds 4, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x000272);  // set thresholds 4, fall
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x27227E);  // set thresholds 3, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x268272);  // set thresholds 3, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x00025A);  // set thresholds 2, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x000251);  // set thresholds 2, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x25025A);  // set thresholds 1, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x245251);  // set thresholds 1, fall

        break;
      }
      case 3: {  // Astro
        // Voltage Regulator
        big_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.duty_cycle = 13;
        big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);

        // CPU Frequency and Scaling
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectRead(0x00000000);  // WaitForBusyCpu
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use

        // SetTarget
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);

        // InitTripPoints
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x000272);  // set thresholds 4, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x000268);  // set thresholds 4, fall
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x266272);  // set thresholds 3, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x25c268);  // set thresholds 3, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x00025A);  // set thresholds 2, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x000251);  // set thresholds 2, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x25025A);  // set thresholds 1, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x245251);  // set thresholds 1, fall
        break;
      }
      case 5: {  // Nelson
        // Voltage Regulator
        big_cluster_pwm_.ExpectEnable(ZX_OK);
        cfg.period_ns = 1500;
        cfg.duty_cycle = 23;
        big_cluster_pwm_.ExpectSetConfig(ZX_OK, cfg);

        // CPU Frequency and Scaling
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectRead(0x00000000);  // WaitForBusyCpu
        (*cpufreq_scaling_mock_hiu_mmio_)[412]
            .ExpectRead(0x00000000)
            .ExpectWrite(0x00010400);  // Dynamic mux 0 is in use

        // SetTarget
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectRead(0x00000000);
        (*cpufreq_scaling_mock_hiu_mmio_)[412].ExpectRead(0x00000000).ExpectWrite(0x00000800);

        // InitTripPoints
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x00029D);  // set thresholds 4, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x000299);  // set thresholds 4, fall
        tsensor_mmio[(0x5 << 2)].ExpectWrite(0x26329D);  // set thresholds 3, rise
        tsensor_mmio[(0x7 << 2)].ExpectWrite(0x24A299);  // set thresholds 3, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x000257);  // set thresholds 2, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x00023F);  // set thresholds 2, fall
        tsensor_mmio[(0x4 << 2)].ExpectWrite(0x236257);  // set thresholds 1, rise
        tsensor_mmio[(0x6 << 2)].ExpectWrite(0x21F23F);  // set thresholds 1, fall
        break;
      }
      default:
        zxlogf(ERROR, "AmlThermalTest::Create: unsupported SOC PID %u", pid);
        return;
    }

    ddk::MmioBuffer tsensor_pll_mmio(tsensor_mock_pll_mmio_->GetMmioBuffer());
    ddk::MmioBuffer tsensor_trim_mmio(tsensor_mock_trim_mmio_->GetMmioBuffer());
    ddk::MmioBuffer tsensor_hiu_mmio(tsensor_mock_hiu_mmio_->GetMmioBuffer());
    auto big_cluster_pwm = big_cluster_pwm_.GetProto();
    auto little_cluster_pwm = little_cluster_pwm_.GetProto();
    ddk::MmioBuffer cpufreq_scaling_hiu_mmio(cpufreq_scaling_mock_hiu_mmio_->GetMmioBuffer());
    thermal_device_ = FakeAmlThermal::Create(
        std::move(tsensor_pll_mmio), std::move(tsensor_trim_mmio), std::move(tsensor_hiu_mmio),
        big_cluster_pwm, little_cluster_pwm, std::move(cpufreq_scaling_hiu_mmio),
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
  fbl::Array<ddk_mock::MockMmioReg> tsensor_trim_regs_;
  fbl::Array<ddk_mock::MockMmioReg> tsensor_hiu_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_pll_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_trim_mmio_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> tsensor_mock_hiu_mmio_;

  // Voltage Regulator
  ddk::MockPwm big_cluster_pwm_;
  ddk::MockPwm little_cluster_pwm_;

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

TEST_F(AmlThermalTest, NelsonInitTest) { Create(5); }

}  // namespace thermal
