// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ti-ina231.h"

#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

}  // namespace

namespace power_sensor {

class FakeIna231Device : public fake_i2c::FakeI2c {
 public:
  FakeIna231Device() {
    // Set bits 15 and 14. Bit 15 (reset) should be masked off, while 14 should be preserved.
    registers_[0] = 0xc000;
  }

  uint16_t configuration() const { return registers_[0]; }
  uint16_t calibration() const { return registers_[5]; }
  uint16_t mask_enable() const { return registers_[6]; }
  uint16_t alert_limit() const { return registers_[7]; }

  void set_bus_voltage(uint16_t voltage) { registers_[2] = voltage; }
  void set_power(uint16_t power) { registers_[3] = power; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size < 1 || write_buffer[0] >= countof(registers_)) {
      return ZX_ERR_IO;
    }

    if (write_buffer_size == 1) {
      read_buffer[0] = registers_[write_buffer[0]] >> 8;
      read_buffer[1] = registers_[write_buffer[0]] & 0xff;
      *read_buffer_size = 2;
    } else if (write_buffer_size == 3) {
      if (write_buffer[0] >= 1 && write_buffer[0] <= 4) {
        // Write-only registers.
        return ZX_ERR_IO;
      }

      registers_[write_buffer[0]] = (write_buffer[1] << 8) | write_buffer[2];
    }

    return ZX_OK;
  }

 private:
  uint16_t registers_[8] = {};
};

TEST(TiIna231Test, GetPowerWatts) {
  fake_ddk::Bind ddk;
  FakeIna231Device fake_i2c;
  Ina231Device dut(fake_ddk::kFakeParent, 10'000, fake_i2c.GetProto());

  constexpr Ina231Metadata kMetadata = {
      .mode = Ina231Metadata::kModeShuntAndBusContinuous,
      .shunt_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .bus_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .averages = Ina231Metadata::kAverages1024,
      .shunt_resistance_microohm = 10'000,
      .alert = Ina231Metadata::kAlertNone,
  };

  EXPECT_OK(dut.Init(kMetadata));
  EXPECT_EQ(fake_i2c.configuration(), 0x4e97);
  EXPECT_EQ(fake_i2c.calibration(), 2048);
  EXPECT_EQ(fake_i2c.mask_enable(), 0);

  EXPECT_OK(dut.DdkAdd("ti-ina231"));

  fidl::WireSyncClient<power_sensor_fidl::Device> client(std::move(ddk.FidlClient()));

  {
    fake_i2c.set_power(4792);
    auto response = client.GetPowerWatts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().power, 29.95f));
  }

  {
    fake_i2c.set_power(0);
    auto response = client.GetPowerWatts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().power, 0.0f));
  }

  {
    fake_i2c.set_power(65535);
    auto response = client.GetPowerWatts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().power, 409.59375f));
  }
}

TEST(TiIna231Test, SetAlertLimit) {
  fake_ddk::Bind ddk;
  FakeIna231Device fake_i2c;
  Ina231Device dut(fake_ddk::kFakeParent, 10'000, fake_i2c.GetProto());

  constexpr Ina231Metadata kMetadata = {
      .mode = Ina231Metadata::kModeShuntAndBusContinuous,
      .shunt_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .bus_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .averages = Ina231Metadata::kAverages1024,
      .shunt_resistance_microohm = 10'000,
      .bus_voltage_limit_microvolt = 11'000'000,
      .alert = Ina231Metadata::kAlertBusUnderVoltage,
  };

  EXPECT_OK(dut.Init(kMetadata));
  EXPECT_EQ(fake_i2c.configuration(), 0x4e97);
  EXPECT_EQ(fake_i2c.calibration(), 2048);
  EXPECT_EQ(fake_i2c.mask_enable(), 0x1000);
  EXPECT_EQ(fake_i2c.alert_limit(), 0x2260);
}

TEST(TiIna231Test, BanjoClients) {
  fake_ddk::Bind ddk;
  FakeIna231Device fake_i2c;
  Ina231Device dut(fake_ddk::kFakeParent, 10'000, fake_i2c.GetProto());

  constexpr Ina231Metadata kMetadata = {
      .mode = Ina231Metadata::kModeShuntAndBusContinuous,
      .shunt_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .bus_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .averages = Ina231Metadata::kAverages1024,
      .shunt_resistance_microohm = 10'000,
      .bus_voltage_limit_microvolt = 11'000'000,
      .alert = Ina231Metadata::kAlertBusUnderVoltage,
  };

  EXPECT_OK(dut.Init(kMetadata));

  fidl::WireSyncClient<fuchsia_hardware_power_sensor::Device> client1, client2;

  zx::channel server;
  ASSERT_OK(zx::channel::create(0, client1.mutable_channel(), &server));
  ASSERT_OK(dut.PowerSensorConnectServer(std::move(server)));

  ASSERT_OK(zx::channel::create(0, client2.mutable_channel(), &server));
  ASSERT_OK(dut.PowerSensorConnectServer(std::move(server)));

  fake_i2c.set_power(4792);

  {
    auto response = client1.GetPowerWatts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().power, 29.95f));
  }

  {
    auto response = client2.GetPowerWatts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().power, 29.95f));
  }
}

TEST(TiIna231Test, GetVoltageVolts) {
  fake_ddk::Bind ddk;
  FakeIna231Device fake_i2c;
  Ina231Device dut(fake_ddk::kFakeParent, 10'000, fake_i2c.GetProto());

  constexpr Ina231Metadata kMetadata = {
      .mode = Ina231Metadata::kModeShuntAndBusContinuous,
      .shunt_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .bus_voltage_conversion_time = Ina231Metadata::kConversionTime332us,
      .averages = Ina231Metadata::kAverages1024,
      .shunt_resistance_microohm = 10'000,
      .alert = Ina231Metadata::kAlertNone,
  };

  EXPECT_OK(dut.Init(kMetadata));
  EXPECT_EQ(fake_i2c.configuration(), 0x4e97);
  EXPECT_EQ(fake_i2c.calibration(), 2048);
  EXPECT_EQ(fake_i2c.mask_enable(), 0);

  EXPECT_OK(dut.DdkAdd("ti-ina231"));

  fidl::WireSyncClient<power_sensor_fidl::Device> client(std::move(ddk.FidlClient()));

  {
    fake_i2c.set_bus_voltage(9200);
    auto response = client.GetVoltageVolts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().voltage, 11.5f));
  }

  {
    fake_i2c.set_bus_voltage(0);
    auto response = client.GetVoltageVolts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().voltage, 0.0f));
  }

  {
    fake_i2c.set_bus_voltage(65535);
    auto response = client.GetVoltageVolts();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response.value().result.is_err());
    EXPECT_TRUE(FloatNear(response.value().result.response().voltage, 81.91875f));
  }
}

}  // namespace power_sensor
