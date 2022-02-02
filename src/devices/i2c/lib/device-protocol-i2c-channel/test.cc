// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <zircon/errors.h>

#include <zxtest/zxtest.h>

class I2cDevice : public fake_i2c::FakeI2c {
 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    count++;
    // Unique errors below to check for retries.
    switch (count) {
      // clang-format off
      case 1: return ZX_ERR_INTERNAL; break;
      case 2: return ZX_ERR_NOT_SUPPORTED; break;
      case 3: return ZX_ERR_NO_RESOURCES; break;
      case 4: return ZX_ERR_NO_MEMORY; break;
      case 5: *read_buffer_size = 1; return ZX_OK; break;
      default: ZX_ASSERT(0);  // Anything else is an error.
        // clang-format on
    }
    return ZX_OK;
  }

 private:
  size_t count = 0;
};

TEST(I2cChannelTest, NoRetries) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
  // No retry, the first error is returned.
  uint8_t buffer[1] = {0x12};
  constexpr uint8_t kNumberOfRetries = 0;
  auto ret = channel.WriteSyncRetries(buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_INTERNAL);
  EXPECT_EQ(ret.retries, 0);
}

TEST(I2cChannelTest, RetriesAllFail) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
  // 2 retries, corresponding error is returned. The first time Transact is called we get a
  // ZX_ERR_INTERNAL. Then the first retry gives us ZX_ERR_NOT_SUPPORTED and then the second
  // gives us ZX_ERR_NO_RESOURCES.
  constexpr uint8_t kNumberOfRetries = 2;
  uint8_t buffer[1] = {0x34};
  auto ret = channel.ReadSyncRetries(0x56, buffer, sizeof(buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_ERR_NO_RESOURCES);
  EXPECT_EQ(ret.retries, 2);
}

TEST(I2cChannelTest, RetriesOk) {
  I2cDevice i2c_dev;
  ddk::I2cChannel channel(i2c_dev.GetProto());
  // 4 retries requested but no error, return ok.
  uint8_t tx_buffer[1] = {0x78};
  uint8_t rx_buffer[1] = {0x90};
  constexpr uint8_t kNumberOfRetries = 5;
  auto ret = channel.WriteReadSyncRetries(tx_buffer, sizeof(tx_buffer), rx_buffer,
                                          sizeof(rx_buffer), kNumberOfRetries, zx::usec(1));
  EXPECT_EQ(ret.status, ZX_OK);
  EXPECT_EQ(ret.retries, 4);
}
