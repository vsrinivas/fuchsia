// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shtv3.h"

#include <lib/fake-i2c/fake-i2c.h>

#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

}  // namespace

namespace temperature {

class FakeShtv3Device : public fake_i2c::FakeI2c {
 public:
  enum State {
    kNeedReset,
    kIdle,
    kMeasurementStarted,
    kMeasurementDone,
    kError,
  };

  State state() const { return state_; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size == 2) {
      const uint16_t command = (write_buffer[0] << 8) | write_buffer[1];
      if (command == 0x805d) {  // Soft reset
        state_ = kIdle;
        return ZX_OK;
      }
      if (command == 0x7866 && state_ == kIdle) {  // Start measurement
        state_ = kMeasurementStarted;
        return ZX_OK;
      }
    } else if (write_buffer_size == 0) {
      if (state_ == kMeasurementStarted) {
        state_ = kMeasurementDone;
        return ZX_ERR_IO;  // The sensor will NACK reads until the measurement is done.
      }
      if (state_ == kMeasurementDone) {
        state_ = kIdle;
        read_buffer[0] = 0x5f;
        read_buffer[1] = 0xd1;
        *read_buffer_size = 2;
        return ZX_OK;
      }
    }

    state_ = kError;
    return ZX_ERR_IO;
  }

 private:
  State state_ = kNeedReset;
};

TEST(Shtv3Test, ReadTemperature) {
  FakeShtv3Device fake_i2c_;
  EXPECT_EQ(fake_i2c_.state(), FakeShtv3Device::kNeedReset);

  Shtv3Device dut(nullptr, fake_i2c_.GetProto());
  EXPECT_OK(dut.Init());
  EXPECT_EQ(fake_i2c_.state(), FakeShtv3Device::kIdle);

  const zx::status<float> status = dut.ReadTemperature();
  EXPECT_TRUE(status.is_ok());
  EXPECT_TRUE(FloatNear(status.value(), 20.5f));
  EXPECT_EQ(fake_i2c_.state(), FakeShtv3Device::kIdle);
}

}  // namespace temperature
