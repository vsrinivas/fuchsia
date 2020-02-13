// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c_client.h"

#include <lib/device-protocol/i2c-channel.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <zircon/errors.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <fbl/array.h>
#include <hwreg/bitfields.h>
#include <zxtest/zxtest.h>

namespace audio::alc5663 {
namespace {

TEST(I2cClient, Client8Bits) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint8_t> client(channel);

  // Write to I2C bus.
  i2c.ExpectWriteStop({0xaa, 0x11});
  EXPECT_OK(client.Write<uint8_t>(0xaa, 0x11));

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c.ExpectWrite({0xaa});
  i2c.ExpectReadStop({0x22});
  uint8_t data;
  EXPECT_OK(client.Read<uint8_t>(0xaa, &data));
  EXPECT_EQ(data, 0x22);

  i2c.VerifyAndClear();
}

TEST(I2cClient, Client16Bits) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint16_t> client(channel);

  // Write to I2C bus.
  i2c.ExpectWriteStop({0xaa, 0xbb, 0x11, 0x22});
  client.Write<uint16_t>(0xaabb, 0x1122);

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c.ExpectWrite({0xcc, 0xdd});
  i2c.ExpectReadStop({0x33, 0x44});
  uint16_t data;
  EXPECT_OK(client.Read<uint16_t>(0xccdd, &data));
  EXPECT_EQ(data, 0x3344U);

  i2c.VerifyAndClear();
}

TEST(I2cClient, ClientMixedAddrDataSize) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint8_t> client(channel);

  // Write to I2C bus.
  i2c.ExpectWriteStop({0xaa, 0x11, 0x22});
  client.Write<uint16_t>(0xaa, 0x1122);

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c.ExpectWrite({0xbb});
  i2c.ExpectReadStop({0x33, 0x44});
  uint16_t data;
  EXPECT_OK(client.Read<uint16_t>(0xbb, &data));
  EXPECT_EQ(data, 0x3344U);

  i2c.VerifyAndClear();
}

struct TestReg {
  uint16_t data;
  DEF_SUBBIT(data, 1, bit);
  static constexpr uint8_t kAddress = 0xaa;
};

TEST(I2cClient, ReadRegister) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint8_t> client(channel);

  // Exepect read from the address (0xaa).
  i2c.ExpectWrite({0xaa});
  i2c.ExpectReadStop({0x11, 0x22});

  TestReg result;
  EXPECT_OK(ReadRegister<TestReg>(&client, &result));
  EXPECT_EQ(result.data, 0x1122);
}

TEST(I2cClient, WriteRegister) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint8_t> client(channel);

  i2c.ExpectWriteStop({0xaa, 0x11, 0x22});
  TestReg result{/*data=*/0x1122};
  EXPECT_OK(WriteRegister<TestReg>(&client, result));
}

TEST(I2cClient, MapRegister) {
  mock_i2c::MockI2c i2c;
  ddk::I2cChannel channel{i2c.GetProto()};
  I2cClient<uint8_t> client(channel);

  i2c.ExpectWrite({0xaa});
  i2c.ExpectReadStop({0x11, 0x22});
  i2c.ExpectWriteStop({0xaa, 0x33, 0x44});

  EXPECT_OK(MapRegister<TestReg>(&client, [](auto reg) {
    EXPECT_EQ(reg.data, 0x1122);
    return TestReg{/*data=*/0x3344};
  }));
}

}  // namespace
}  // namespace audio::alc5663
