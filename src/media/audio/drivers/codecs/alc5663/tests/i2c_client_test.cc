// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c_client.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <zircon/errors.h>

#include <memory>

#include <fbl/array.h>
#include <hwreg/bitfields.h>
#include <zxtest/zxtest.h>

namespace audio::alc5663 {
namespace {

class I2cClientTest : public zxtest::Test {
 public:
  I2cClientTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &i2c_);

    i2c_client_ = std::move(endpoints->client);

    EXPECT_OK(loop_.StartThread());
  }

 protected:
  mock_i2c::MockI2c& i2c() { return i2c_; }
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> TakeI2cClient() { return std::move(i2c_client_); }

 private:
  mock_i2c::MockI2c i2c_;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c_client_;
  async::Loop loop_;
};

TEST_F(I2cClientTest, Client8Bits) {
  I2cClient<uint8_t> client(TakeI2cClient());

  // Write to I2C bus.
  i2c().ExpectWriteStop({0xaa, 0x11});
  EXPECT_OK(client.Write<uint8_t>(0xaa, 0x11));

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c().ExpectWrite({0xaa});
  i2c().ExpectReadStop({0x22});
  uint8_t data;
  EXPECT_OK(client.Read<uint8_t>(0xaa, &data));
  EXPECT_EQ(data, 0x22);

  i2c().VerifyAndClear();
}

TEST_F(I2cClientTest, Client16Bits) {
  I2cClient<uint16_t> client(TakeI2cClient());

  // Write to I2C bus.
  i2c().ExpectWriteStop({0xaa, 0xbb, 0x11, 0x22});
  client.Write<uint16_t>(0xaabb, 0x1122);

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c().ExpectWrite({0xcc, 0xdd});
  i2c().ExpectReadStop({0x33, 0x44});
  uint16_t data;
  EXPECT_OK(client.Read<uint16_t>(0xccdd, &data));
  EXPECT_EQ(data, 0x3344U);

  i2c().VerifyAndClear();
}

TEST_F(I2cClientTest, ClientMixedAddrDataSize) {
  I2cClient<uint8_t> client(TakeI2cClient());

  // Write to I2C bus.
  i2c().ExpectWriteStop({0xaa, 0x11, 0x22});
  client.Write<uint16_t>(0xaa, 0x1122);

  // Read from I2C bus, which is a write of the address, followed by a read of data.
  i2c().ExpectWrite({0xbb});
  i2c().ExpectReadStop({0x33, 0x44});
  uint16_t data;
  EXPECT_OK(client.Read<uint16_t>(0xbb, &data));
  EXPECT_EQ(data, 0x3344U);

  i2c().VerifyAndClear();
}

struct TestReg {
  uint16_t data;
  DEF_SUBBIT(data, 1, bit);
  static constexpr uint8_t kAddress = 0xaa;
};

TEST_F(I2cClientTest, ReadRegister) {
  I2cClient<uint8_t> client(TakeI2cClient());

  // Exepect read from the address (0xaa).
  i2c().ExpectWrite({0xaa});
  i2c().ExpectReadStop({0x11, 0x22});

  TestReg result;
  EXPECT_OK(ReadRegister<TestReg>(&client, &result));
  EXPECT_EQ(result.data, 0x1122);
}

TEST_F(I2cClientTest, WriteRegister) {
  I2cClient<uint8_t> client(TakeI2cClient());

  i2c().ExpectWriteStop({0xaa, 0x11, 0x22});
  TestReg result{/*data=*/0x1122};
  EXPECT_OK(WriteRegister<TestReg>(&client, result));
}

TEST_F(I2cClientTest, MapRegister) {
  I2cClient<uint8_t> client(TakeI2cClient());

  i2c().ExpectWrite({0xaa});
  i2c().ExpectReadStop({0x11, 0x22});
  i2c().ExpectWriteStop({0xaa, 0x33, 0x44});

  EXPECT_OK(MapRegister<TestReg>(&client, [](auto reg) {
    EXPECT_EQ(reg.data, 0x1122);
    return TestReg{/*data=*/0x3344};
  }));
}

}  // namespace
}  // namespace audio::alc5663
