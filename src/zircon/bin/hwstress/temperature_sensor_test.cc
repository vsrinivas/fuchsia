// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "temperature_sensor.h"

#include <fuchsia/hardware/thermal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <cmath>
#include <optional>

#include <gtest/gtest.h>

#include "testing_util.h"

namespace hwstress {
namespace {

class FakeThermalServer : public fuchsia::hardware::thermal::Device {
 public:
  explicit FakeThermalServer(float reported_temperature)
      : reported_temperature_(reported_temperature) {}

  void GetTemperatureCelsius(
      fuchsia::hardware::thermal::Device::GetTemperatureCelsiusCallback callback) override {
    callback(ZX_OK, reported_temperature_);
  }

  // Implementation of |Device| methods we don't care about.
  void GetInfo(GetInfoCallback callback) override {}
  void GetDeviceInfo(GetDeviceInfoCallback callback) override {}
  void GetDvfsInfo(fuchsia::hardware::thermal::PowerDomain power_domain,
                   GetDvfsInfoCallback callback) override {}
  void GetStateChangeEvent(GetStateChangeEventCallback callback) override {}
  void GetStateChangePort(GetStateChangePortCallback callback) override {}
  void SetTripCelsius(uint32_t id, float temp, SetTripCelsiusCallback callback) override {}
  void GetDvfsOperatingPoint(fuchsia::hardware::thermal::PowerDomain power_domain,
                             GetDvfsOperatingPointCallback callback) override {}
  void SetDvfsOperatingPoint(uint16_t op_idx, fuchsia::hardware::thermal::PowerDomain power_domain,
                             SetDvfsOperatingPointCallback callback) override {}
  void GetFanLevel(GetFanLevelCallback callback) override {}
  void SetFanLevel(uint32_t fan_level, SetFanLevelCallback callback) override {}

 private:
  // Temperature to report back.
  float reported_temperature_;
};

TEST(TemperatureSensor, NullSensor) {
  ASSERT_EQ(std::nullopt, CreateNullTemperatureSensor()->ReadCelcius());
}

TEST(TemperatureSensor, SystemTemperatureSensor) {
  auto factory = std::make_unique<testing::LoopbackConnectionFactory>();

  // Create a server that always reports a single, static value.
  FakeThermalServer server{42.0};

  // Create a channel.
  zx::channel client = factory->CreateChannelTo<fuchsia::hardware::thermal::Device>(&server);

  // Ensure we can read from the server.
  auto sensor = CreateSystemTemperatureSensor(std::move(client));
  ASSERT_EQ(42.0, sensor->ReadCelcius());
  ASSERT_EQ(42.0, sensor->ReadCelcius());
  ASSERT_EQ(42.0, sensor->ReadCelcius());

  // Close the connection. Ensure that we get nullopt results.
  factory.reset();
  ASSERT_EQ(std::nullopt, sensor->ReadCelcius());
}

TEST(TemperatureToString, Basic) {
  // Normal values.
  EXPECT_EQ(TemperatureToString(1.0), "1.0°C");
  EXPECT_EQ(TemperatureToString(-1.0), "-1.0°C");
  EXPECT_EQ(TemperatureToString(100.0), "100.0°C");
  EXPECT_EQ(TemperatureToString(3.14159265359), "3.1°C");

  // Unknown value.
  EXPECT_EQ(TemperatureToString(std::nullopt), "unknown");

  // We don't expect these temperatures, but we shouldn't crash.
  EXPECT_EQ(TemperatureToString(std::numeric_limits<double>::infinity()), "inf°C");
  EXPECT_EQ(TemperatureToString(std::nan("")), "nan°C");
}

}  // namespace
}  // namespace hwstress
