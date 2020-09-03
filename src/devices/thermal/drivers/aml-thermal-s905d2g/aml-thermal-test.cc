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

    auto config = sherlock_thermal_config;
    if (less) {
      config.num_trip_points = 2;
      config.trip_point_info[2].up_temp_celsius = -273.15f + 2.0f;
    }

    EXPECT_OK(test->InitSensor(config));
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
      zxlogf(ERROR, "AmlTSensorTest::SetUp: pll_regs_ alloc failed");
      return;
    }
    mock_pll_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, pll_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_pll_mmio_ alloc failed");
      return;
    }

    ao_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: ao_regs_ alloc failed");
      return;
    }
    mock_ao_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ao_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "AmlTSensorTest::SetUp: mock_ao_mmio_ alloc failed");
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

    (*mock_ao_mmio_)[0x268].ExpectRead(0x00000000);                           // trim_info_
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

}  // namespace thermal
