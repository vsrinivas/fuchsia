// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-thermal.h"

#include <fuchsia/hardware/clock/cpp/banjo-mock.h>
#include <fuchsia/hardware/power/cpp/banjo-mock.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fake_ddk/fidl-helper.h>

#include <fbl/algorithm.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

}  // namespace

namespace thermal {

using ThermalClient = fuchsia_hardware_thermal::Device::SyncClient;
using fuchsia_hardware_thermal::wire::OperatingPoint;
using fuchsia_hardware_thermal::wire::OperatingPointEntry;
using fuchsia_hardware_thermal::wire::PowerDomain;
using fuchsia_hardware_thermal::wire::ThermalDeviceInfo;

class As370ThermalTest : public zxtest::Test {
 public:
  As370ThermalTest()
      : reg_region_(reg_array_, sizeof(uint32_t), std::size(reg_array_)),
        dut_(nullptr, ddk::MmioBuffer(reg_region_.GetMmioBuffer()), kThermalDeviceInfo,
             ddk::ClockProtocolClient(clock_.GetProto()),
             ddk::PowerProtocolClient(power_.GetProto())) {}

  void SetUp() override {
    ASSERT_OK(messenger_.SetMessageOp(
        &dut_, [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
          return static_cast<As370Thermal*>(ctx)->DdkMessage(msg, txn);
        }));
  }

 protected:
  void VerifyAll() {
    ASSERT_NO_FATAL_FAILURES(clock_.VerifyAndClear());
    ASSERT_NO_FATAL_FAILURES(power_.VerifyAndClear());
  }

  fake_ddk::FidlMessenger messenger_;
  ddk_mock::MockMmioRegRegion reg_region_;
  ddk::MockClock clock_;
  ddk::MockPower power_;
  As370Thermal dut_;

 private:
  static constexpr ThermalDeviceInfo kThermalDeviceInfo = {
      .active_cooling = false,
      .passive_cooling = true,
      .gpu_throttling = false,
      .num_trip_points = 0,
      .big_little = false,
      .critical_temp_celsius = 0.0f,
      .trip_point_info = {},
      .opps =
          fidl::Array<OperatingPoint, 2>{
              OperatingPoint{
                  .opp =
                      fidl::Array<OperatingPointEntry, 16>{
                          // clang-format off
                          OperatingPointEntry{.freq_hz =   400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz =   800'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'200'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'400'000'000, .volt_uv = 825'000},
                          OperatingPointEntry{.freq_hz = 1'500'000'000, .volt_uv = 900'000},
                          OperatingPointEntry{.freq_hz = 1'800'000'000, .volt_uv = 900'000},
                          // clang-format on
                      },
                  .latency = 0,
                  .count = 6,
              },
              {
                  .opp = {},
                  .latency = 0,
                  .count = 0,
              },
          },
  };

  ddk_mock::MockMmioReg reg_array_[8];
};

TEST_F(As370ThermalTest, GetTemperature) {
  ThermalClient client(std::move(messenger_.local()));

  {
    reg_region_[0x14].ReadReturns(0x17ff);
    auto result = client.GetTemperatureCelsius();
    EXPECT_OK(result->status);
    EXPECT_TRUE(FloatNear(result->temp, 40.314f));
  }

  {
    reg_region_[0x14].ReadReturns(0x182b);
    auto result = client.GetTemperatureCelsius();
    EXPECT_OK(result->status);
    EXPECT_TRUE(FloatNear(result->temp, 43.019f));
  }
}

TEST_F(As370ThermalTest, DvfsOperatingPoint) {
  ThermalClient client(std::move(messenger_.local()));

  {
    // Success, sets operating point 0.
    power_.ExpectRequestVoltage(ZX_OK, 825'000, 825'000);
    clock_.ExpectSetRate(ZX_OK, 400'000'000);
    auto set_result = client.SetDvfsOperatingPoint(0, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 0);
  }

  {
    // Failure, unable to set exact voltage.
    power_.ExpectRequestVoltage(ZX_OK, 825'000, 900'000);
    auto set_result = client.SetDvfsOperatingPoint(2, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_NOT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 0);
  }

  {
    // Failure, unable to set frequency.
    power_.ExpectRequestVoltage(ZX_OK, 825'000, 825'000);
    clock_.ExpectSetRate(ZX_ERR_IO, 1'200'000'000);
    auto set_result = client.SetDvfsOperatingPoint(2, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_NOT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 0);
  }

  {
    // Success, sets operating point 4.
    power_.ExpectRequestVoltage(ZX_OK, 900'000, 900'000);
    clock_.ExpectSetRate(ZX_OK, 1'500'000'000);
    auto set_result = client.SetDvfsOperatingPoint(4, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 4);
  }

  {
    // Failure, unable to set frequency.
    clock_.ExpectSetRate(ZX_ERR_IO, 800'000'000);
    auto set_result = client.SetDvfsOperatingPoint(1, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_NOT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 4);
  }

  {
    // Failure, unable to set voltage.
    clock_.ExpectSetRate(ZX_OK, 800'000'000);
    power_.ExpectRequestVoltage(ZX_ERR_IO, 825'000, 0);
    auto set_result = client.SetDvfsOperatingPoint(1, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_NOT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 4);
  }

  {
    // Success, sets operating point 1.
    clock_.ExpectSetRate(ZX_OK, 800'000'000);
    power_.ExpectRequestVoltage(ZX_OK, 825'000, 825'000);
    auto set_result = client.SetDvfsOperatingPoint(1, PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(set_result->status);
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  {
    auto get_result = client.GetDvfsOperatingPoint(PowerDomain::BIG_CLUSTER_POWER_DOMAIN);
    EXPECT_OK(get_result->status);
    EXPECT_EQ(get_result->op_idx, 1);
  }
}

TEST_F(As370ThermalTest, Init) {
  power_.ExpectRegisterPowerDomain(ZX_OK, 825'000, 900'000);
  power_.ExpectRequestVoltage(ZX_OK, 900'000, 900'000);
  clock_.ExpectSetRate(ZX_OK, 1'800'000'000);
  EXPECT_OK(dut_.Init());
  ASSERT_NO_FATAL_FAILURES(VerifyAll());
}

}  // namespace thermal
