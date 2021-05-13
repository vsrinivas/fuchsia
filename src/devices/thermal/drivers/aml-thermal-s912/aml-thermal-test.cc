// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <fuchsia/hardware/scpi/cpp/banjo-mock.h>
#include <fuchsia/hardware/thermal/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/metadata.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

fuchsia_hardware_thermal::wire::ThermalTemperatureInfo TripPointInfo(
    float up_temp, float down_temp, uint16_t big_cluster_dvfs_opp,
    uint16_t little_cluster_dvfs_opp) {
  return {.up_temp_celsius = up_temp,
          .down_temp_celsius = down_temp,
          .fan_level = 0,
          .big_cluster_dvfs_opp = big_cluster_dvfs_opp,
          .little_cluster_dvfs_opp = little_cluster_dvfs_opp,
          .gpu_clk_freq_source = 0};
}

// vim2 thermal device info.
fuchsia_hardware_thermal::wire::ThermalDeviceInfo kDeviceInfo = {
    .active_cooling = true,
    .passive_cooling = true,
    .gpu_throttling = true,
    .num_trip_points = 8,
    .big_little = true,
    .critical_temp_celsius = 81.0f,
    .trip_point_info =
        {
            TripPointInfo(2.0f, 0.0f, 6, 4),
            TripPointInfo(65.0f, 63.0f, 6, 4),
            TripPointInfo(70.0f, 68.0f, 6, 4),
            TripPointInfo(75.0f, 73.0f, 6, 4),
            TripPointInfo(82.0f, 79.0f, 5, 4),
            TripPointInfo(87.0f, 84.0f, 4, 4),
            TripPointInfo(92.0f, 89.0f, 3, 3),
            TripPointInfo(96.0f, 93.0f, 2, 2),
        },
    .opps = {},
};

}  // namespace

namespace thermal {

// Customize ddk::MockScpi to allow ScpiGetSensorValue to return a default value after all
// expectations have been used.

class MockScpi : public ddk::MockScpi {
 public:
  MockScpi& ExpectGetSensorValue(zx_status_t out_s, uint32_t sensor_id,
                                 uint32_t out_sensor_value) override {
    last_sensor_value_ = {out_s, out_sensor_value};
    get_sensor_value_expectations_++;

    ddk::MockScpi::ExpectGetSensorValue(out_s, sensor_id, out_sensor_value);
    return *this;
  }

  zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* out_sensor_value) override {
    if (get_sensor_value_expectations_ == 0) {
      *out_sensor_value = std::get<1>(last_sensor_value_);
      return std::get<0>(last_sensor_value_);
    }

    get_sensor_value_expectations_--;
    return ddk::MockScpi::ScpiGetSensorValue(sensor_id, out_sensor_value);
  }

 private:
  uint32_t get_sensor_value_expectations_ = 0;
  std::tuple<zx_status_t, uint32_t> last_sensor_value_ = {ZX_OK, 0};
};

class AmlThermalTest : public zxtest::Test {
 public:
  AmlThermalTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {
    fidl::OwnedEncodedMessage<fthermal::wire::ThermalDeviceInfo> encoded_metadata(&kDeviceInfo);
    ASSERT_OK(encoded_metadata.status());
    encoded_metadata_ = encoded_metadata.GetOutgoingMessage().CopyBytes();
  }

  void StartFidlServer(AmlThermal* device) {
    auto endpoints = fidl::CreateEndpoints<fthermal::Device>();
    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), device);
    client_ = fidl::BindSyncClient(std::move(endpoints->client));
    ASSERT_OK(loop_.StartThread("aml-thermal-test-loop"));
  }

 protected:
  fidl::WireSyncClient<fthermal::Device> client_;

  fidl::OutgoingMessage::CopiedBytes encoded_metadata_;

 private:
  async::Loop loop_;
};

TEST_F(AmlThermalTest, GetDvfsInfo) {
  constexpr scpi_opp_t kExpectedBigClusterOpp = {
      .opp =
          {
              [0] = {.freq_hz = 500'000'000, .volt_uv = 900'000},
              [1] = {.freq_hz = 750'000'000, .volt_uv = 900'000},
              [2] = {.freq_hz = 1'000'000'000, .volt_uv = 1'000'000},
              [3] = {.freq_hz = 1'100'000'000, .volt_uv = 1'000'000},
              [4] = {.freq_hz = 1'200'000'000, .volt_uv = 1'100'000},
          },
      .latency = 100,
      .count = 5};

  constexpr scpi_opp_t kExpectedLittleClusterOpp = {
      .opp =
          {
              [0] = {.freq_hz = 500'000'000, .volt_uv = 800'000},
              [1] = {.freq_hz = 650'000'000, .volt_uv = 900'000},
              [2] = {.freq_hz = 900'000'000, .volt_uv = 1'000'000},
          },
      .latency = 200,
      .count = 3};

  MockScpi scpi;
  AmlThermal dut(nullptr, {}, {}, scpi.GetProto(), 0, zx::port(), fake_ddk::kFakeDevice);

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  scpi.ExpectGetDvfsInfo(ZX_ERR_IO,
                         static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain),
                         kExpectedBigClusterOpp)
      .ExpectGetDvfsInfo(ZX_OK,
                         static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain),
                         kExpectedBigClusterOpp)
      .ExpectGetDvfsInfo(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain),
          kExpectedLittleClusterOpp);

  auto result = client_.GetDvfsInfo(fthermal::wire::PowerDomain::kBigClusterPowerDomain);
  ASSERT_OK(result.status());
  EXPECT_EQ(result->status, ZX_ERR_IO);

  auto result2 = client_.GetDvfsInfo(fthermal::wire::PowerDomain::kBigClusterPowerDomain);
  EXPECT_OK(result2->status);
  EXPECT_BYTES_EQ(result2->info.get(), &kExpectedBigClusterOpp, sizeof(scpi_opp_t));

  auto result3 = client_.GetDvfsInfo(fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
  ASSERT_OK(result3.status());
  ASSERT_OK(result3->status);
  EXPECT_BYTES_EQ(result3->info.get(), &kExpectedLittleClusterOpp, sizeof(scpi_opp_t));

  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, DvfsOperatingPoint) {
  MockScpi scpi;
  AmlThermal dut(nullptr, {}, {}, scpi.GetProto(), 0, zx::port(), fake_ddk::kFakeDevice);

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  scpi.ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 1)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), 3)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 0)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), 10)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 7);

  {
    auto result =
        client_.SetDvfsOperatingPoint(1, fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.GetDvfsOperatingPoint(fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->op_idx, 1);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(3, fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.GetDvfsOperatingPoint(fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->op_idx, 3);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(0, fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.GetDvfsOperatingPoint(fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->op_idx, 0);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(0, fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(10, fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.GetDvfsOperatingPoint(fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->op_idx, 10);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(10, fthermal::wire::PowerDomain::kLittleClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.SetDvfsOperatingPoint(7, fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result =
        client_.GetDvfsOperatingPoint(fthermal::wire::PowerDomain::kBigClusterPowerDomain);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->op_idx, 7);
  }

  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, FanLevel) {
  ddk::MockGpio fan0, fan1;
  AmlThermal dut(nullptr, fan0.GetProto(), fan1.GetProto(), {}, 0, zx::port(),
                 fake_ddk::kFakeDevice);

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  fan0.ExpectWrite(ZX_OK, 0)
      .ExpectWrite(ZX_OK, 0)
      .ExpectWrite(ZX_OK, 1)
      .ExpectWrite(ZX_OK, 1)
      .ExpectWrite(ZX_OK, 0);
  fan1.ExpectWrite(ZX_OK, 0)
      .ExpectWrite(ZX_OK, 1)
      .ExpectWrite(ZX_OK, 0)
      .ExpectWrite(ZX_OK, 1)
      .ExpectWrite(ZX_OK, 0);

  {
    auto result = client_.SetFanLevel(FAN_L0);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result = client_.GetFanLevel();
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->fan_level, FAN_L0);
  }
  {
    auto result = client_.SetFanLevel(FAN_L2);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result = client_.GetFanLevel();
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->fan_level, FAN_L2);
  }
  {
    auto result = client_.SetFanLevel(FAN_L1);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result = client_.GetFanLevel();
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->fan_level, FAN_L1);
  }
  {
    auto result = client_.SetFanLevel(FAN_L3);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result = client_.GetFanLevel();
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->fan_level, FAN_L3);
  }
  {
    auto result = client_.SetFanLevel(FAN_L0);
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
  }
  {
    auto result = client_.GetFanLevel();
    ASSERT_OK(result.status());
    EXPECT_OK(result->status);
    EXPECT_EQ(result->fan_level, FAN_L0);
  }

  fan0.VerifyAndClear();
  fan1.VerifyAndClear();
}

TEST_F(AmlThermalTest, TripPointThread) {
  fake_ddk::Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_THERMAL_CONFIG, encoded_metadata_.data(),
                  encoded_metadata_.size());

  ddk::MockGpio fan0, fan1;
  MockScpi scpi;

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  zx::unowned_port port_ref(port.get());

  AmlThermal dut(fake_ddk::kFakeDevice, fan0.GetProto(), fan1.GetProto(), scpi.GetProto(), 1234,
                 std::move(port), fake_ddk::kFakeDevice, zx::msec(10));

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  auto result = client_.GetStateChangePort();
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
  ASSERT_TRUE(result->handle.is_valid());

  fan0.ExpectConfigOut(ZX_OK, 0);
  fan1.ExpectConfigOut(ZX_OK, 0);

  scpi.ExpectGetDvfsInfo(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), {})
      .ExpectGetDvfsInfo(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), {})
      .ExpectGetSensorValue(ZX_OK, 1234, 30.0f)  // Trip point 0
      .ExpectGetSensorValue(ZX_OK, 1234, 75.0f)  // 0 -> 1
      .ExpectGetSensorValue(ZX_OK, 1234, 75.0f)  // 1 -> 2
      .ExpectGetSensorValue(ZX_OK, 1234, 75.0f)  // 2 -> 3
      .ExpectGetSensorValue(ZX_OK, 1234, 67.0f)  // 3 -> 2
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 2 -> 3
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 3 -> 4
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 4 -> 5
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 5 -> 6
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 6 -> 7
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 7 -> critical
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 0)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), 0)
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)
      .ExpectGetSensorValue(ZX_OK, 1234, 78.0f)  // 7 -> 6
      .ExpectGetSensorValue(ZX_OK, 1234, 78.0f)  // 6 -> 5
      .ExpectGetSensorValue(ZX_OK, 1234, 78.0f)  // 5 -> 4
      .ExpectGetSensorValue(ZX_OK, 1234, 87.0f)  // 4 -> 5
      .ExpectGetSensorValue(ZX_OK, 1234, 87.0f)
      .ExpectGetSensorValue(ZX_OK, 1234, 87.0f)
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 5 -> 6
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 6 -> 7
      .ExpectGetSensorValue(ZX_OK, 1234, 96.0f)  // 7 -> critical
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), 0)
      .ExpectSetDvfsIdx(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), 0);

  ASSERT_OK(dut.Init(fake_ddk::kFakeDevice));

  zx_port_packet_t packet;

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 0);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 3);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 3);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port_ref->wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  auto result2 = client_.GetTemperatureCelsius();
  ASSERT_OK(result2.status());
  ASSERT_OK(result2->status);
  ASSERT_TRUE(FloatNear(result2->temp, 96.0f));

  dut.DdkUnbind(ddk::UnbindTxn(fake_ddk::kFakeDevice));
  dut.JoinWorkerThread();

  fan0.VerifyAndClear();
  fan1.VerifyAndClear();
  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, DdkLifecycle) {
  fake_ddk::Bind ddk;
  ddk.SetMetadata(DEVICE_METADATA_THERMAL_CONFIG, encoded_metadata_.data(),
                  encoded_metadata_.size());

  ddk::MockGpio fan0, fan1;
  MockScpi scpi;

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  AmlThermal dut(fake_ddk::kFakeParent, fan0.GetProto(), fan1.GetProto(), scpi.GetProto(), 1234,
                 std::move(port), fake_ddk::kFakeDevice, zx::msec(10));

  fan0.ExpectConfigOut(ZX_OK, 0);
  fan1.ExpectConfigOut(ZX_OK, 0);

  scpi.ExpectGetDvfsInfo(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kBigClusterPowerDomain), {})
      .ExpectGetDvfsInfo(
          ZX_OK, static_cast<uint8_t>(fthermal::wire::PowerDomain::kLittleClusterPowerDomain), {});

  // The DdkInit hook will run after DdkAdd.
  dut.DdkAdd("vim-thermal");
  dut.DdkAsyncRemove();

  EXPECT_TRUE(ddk.Ok());

  // Join the worker thread spawned during the DdkInit hook.
  dut.JoinWorkerThread();

  fan0.VerifyAndClear();
  fan1.VerifyAndClear();
  scpi.VerifyAndClear();
}

}  // namespace thermal
