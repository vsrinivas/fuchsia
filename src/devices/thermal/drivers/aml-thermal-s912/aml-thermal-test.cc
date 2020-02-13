// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-thermal.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <mock/ddktl/protocol/gpio.h>
#include <mock/ddktl/protocol/scpi.h>
#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPointInfo(
    float up_temp, float down_temp, uint16_t big_cluster_dvfs_opp,
    uint16_t little_cluster_dvfs_opp) {
  return {.up_temp_celsius = up_temp,
          .down_temp_celsius = down_temp,
          .fan_level = 0,
          .big_cluster_dvfs_opp = big_cluster_dvfs_opp,
          .little_cluster_dvfs_opp = little_cluster_dvfs_opp,
          .gpu_clk_freq_source = 0};
}

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
  AmlThermalTest() : loop_(&kAsyncLoopConfigAttachToCurrentThread) {}

  void StartFidlServer(AmlThermal* device) {
    zx::channel server;
    ASSERT_OK(zx::channel::create(0, &client_, &server));
    ASSERT_OK(
        fidl_bind(loop_.dispatcher(), server.release(),
                  reinterpret_cast<fidl_dispatch_t*>(fuchsia_hardware_thermal_Device_dispatch),
                  device, &AmlThermal::fidl_ops));
    ASSERT_OK(loop_.StartThread("aml-thermal-test-loop"));
  }

 protected:
  zx::channel client_;

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
  AmlThermal dut(nullptr, {}, {}, *scpi.GetProto(), 0, zx::port());

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  scpi.ExpectGetDvfsInfo(ZX_ERR_IO, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN,
                         kExpectedBigClusterOpp)
      .ExpectGetDvfsInfo(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN,
                         kExpectedBigClusterOpp)
      .ExpectGetDvfsInfo(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
                         kExpectedLittleClusterOpp);

  zx_status_t status;
  scpi_opp_t out_opp;

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsInfo(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status,
      &out_opp));
  EXPECT_EQ(status, ZX_ERR_IO);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsInfo(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status,
      &out_opp));
  EXPECT_OK(status);
  EXPECT_BYTES_EQ(&out_opp, &kExpectedBigClusterOpp, sizeof(scpi_opp_t));

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsInfo(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, &status,
      &out_opp));
  EXPECT_OK(status);
  EXPECT_BYTES_EQ(&out_opp, &kExpectedLittleClusterOpp, sizeof(scpi_opp_t));

  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, DvfsOperatingPoint) {
  MockScpi scpi;
  AmlThermal dut(nullptr, {}, {}, *scpi.GetProto(), 0, zx::port());

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  scpi.ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, 1)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, 3)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, 0)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, 10)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, 7);

  zx_status_t status;
  uint16_t op_idx;

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 1, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status,
      &op_idx));
  EXPECT_OK(status);
  EXPECT_EQ(op_idx, 1);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 3, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, &status,
      &op_idx));
  EXPECT_OK(status);
  EXPECT_EQ(op_idx, 3);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 0, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status,
      &op_idx));
  EXPECT_OK(status);
  EXPECT_EQ(op_idx, 0);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 0, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 10, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
      &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, &status,
      &op_idx));
  EXPECT_OK(status);
  EXPECT_EQ(op_idx, 10);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 10, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
      &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(
      client_.get(), 7, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(
      client_.get(), fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, &status,
      &op_idx));
  EXPECT_OK(status);
  EXPECT_EQ(op_idx, 7);

  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, FanLevel) {
  ddk::MockGpio fan0, fan1;
  AmlThermal dut(nullptr, *fan0.GetProto(), *fan1.GetProto(), {}, 0, zx::port());

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

  zx_status_t status;
  uint32_t fan_level;

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetFanLevel(client_.get(), FAN_L0, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetFanLevel(client_.get(), &status, &fan_level));
  EXPECT_OK(status);
  EXPECT_EQ(fan_level, FAN_L0);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetFanLevel(client_.get(), FAN_L2, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetFanLevel(client_.get(), &status, &fan_level));
  EXPECT_OK(status);
  EXPECT_EQ(fan_level, FAN_L2);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetFanLevel(client_.get(), FAN_L1, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetFanLevel(client_.get(), &status, &fan_level));
  EXPECT_OK(status);
  EXPECT_EQ(fan_level, FAN_L1);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetFanLevel(client_.get(), FAN_L3, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetFanLevel(client_.get(), &status, &fan_level));
  EXPECT_OK(status);
  EXPECT_EQ(fan_level, FAN_L3);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceSetFanLevel(client_.get(), FAN_L0, &status));
  EXPECT_OK(status);

  EXPECT_OK(fuchsia_hardware_thermal_DeviceGetFanLevel(client_.get(), &status, &fan_level));
  EXPECT_OK(status);
  EXPECT_EQ(fan_level, FAN_L0);

  fan0.VerifyAndClear();
  fan1.VerifyAndClear();
}

TEST_F(AmlThermalTest, TripPointThread) {
  // vim2 thermal device info.
  constexpr fuchsia_hardware_thermal_ThermalDeviceInfo kDeviceInfo = {
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

  fake_ddk::Bind ddk;
  ddk.SetMetadata(&kDeviceInfo, sizeof(kDeviceInfo));

  ddk::MockGpio fan0, fan1;
  MockScpi scpi;

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  AmlThermal dut(fake_ddk::kFakeDevice, *fan0.GetProto(), *fan1.GetProto(), *scpi.GetProto(), 1234,
                 std::move(port), zx::msec(10));

  ASSERT_NO_FATAL_FAILURES(StartFidlServer(&dut));

  zx_status_t status;

  ASSERT_OK(fuchsia_hardware_thermal_DeviceGetStateChangePort(client_.get(), &status,
                                                              port.reset_and_get_address()));
  ASSERT_OK(status);
  ASSERT_TRUE(port.is_valid());

  fan0.ExpectConfigOut(ZX_OK, 0);
  fan1.ExpectConfigOut(ZX_OK, 0);

  scpi.ExpectGetDvfsInfo(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, {})
      .ExpectGetDvfsInfo(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN,
                         {})
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
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, 0)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, 0)
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
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN, 0)
      .ExpectSetDvfsIdx(ZX_OK, fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN, 0);

  ASSERT_OK(dut.Init(fake_ddk::kFakeDevice));

  zx_port_packet_t packet;

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 0);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 3);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 3);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 5);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 6);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  ASSERT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);

  float temperature;

  ASSERT_OK(
      fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(client_.get(), &status, &temperature));
  ASSERT_OK(status);
  ASSERT_TRUE(FloatNear(temperature, 96.0f));

  dut.DdkUnbindNew(ddk::UnbindTxn(fake_ddk::kFakeDevice));
  dut.JoinWorkerThread();

  fan0.VerifyAndClear();
  fan1.VerifyAndClear();
  scpi.VerifyAndClear();
}

TEST_F(AmlThermalTest, DdkLifecycle) {
  fake_ddk::Bind ddk;
  AmlThermal dut(fake_ddk::kFakeParent, {}, {}, {}, 0, zx::port());

  dut.DdkAdd("vim-thermal", DEVICE_ADD_INVISIBLE);
  dut.DdkAsyncRemove();

  EXPECT_TRUE(ddk.Ok());
}

}  // namespace thermal
