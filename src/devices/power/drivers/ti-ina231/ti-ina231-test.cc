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
  uint16_t configuration() const { return configuration_; }
  uint16_t calibration() const { return calibration_; }

  void set_power(uint16_t power) { power_ = power; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size == 1) {
      if (write_buffer[0] == 0) {
        read_buffer[0] = configuration_ >> 8;
        read_buffer[1] = configuration_ & 0xff;
        *read_buffer_size = 2;
        return ZX_OK;
      }
      if (write_buffer[0] == 3) {
        read_buffer[0] = power_ >> 8;
        read_buffer[1] = power_ & 0xff;
        *read_buffer_size = 2;
        return ZX_OK;
      }
    } else if (write_buffer_size == 3) {
      const uint16_t value = (write_buffer[1] << 8) | write_buffer[2];
      if (write_buffer[0] == 0) {
        configuration_ = value;
        return ZX_OK;
      }
      if (write_buffer[0] == 5) {
        calibration_ = value;
        return ZX_OK;
      }
    }

    return ZX_ERR_IO;
  }

 private:
  uint16_t configuration_ = 0;
  uint16_t power_ = 0;
  uint16_t calibration_ = 0;
};

TEST(TiIna231Test, GetPowerWatts) {
  fake_ddk::Bind ddk;
  FakeIna231Device fake_i2c;
  Ina231Device dut(fake_ddk::kFakeParent, 10'000, fake_i2c.GetProto());

  EXPECT_OK(dut.Init());
  EXPECT_EQ(fake_i2c.configuration(), 0b11);
  EXPECT_EQ(fake_i2c.calibration(), 2048);

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

}  // namespace power_sensor
