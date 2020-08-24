// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-i2c/fake-i2c.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <zxtest/zxtest.h>

#include "src/graphics/display/drivers/ssd1306/ssd1306.h"

namespace ssd1306 {

class FakeI2cParent : public fake_i2c::FakeI2c {
 private:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (read_buffer_size) {
      *read_buffer_size = 0;
    }
    return ZX_OK;
  }
};

class Ssd1306Test : public zxtest::Test {
  void SetUp() override {}

  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
};

TEST_F(Ssd1306Test, LifetimeTest) {
  FakeI2cParent parent;
  ddk::I2cChannel i2c_channel = ddk::I2cChannel(parent.GetProto());
  auto device = new Ssd1306(fake_ddk::kFakeParent);

  ASSERT_OK(device->Bind(i2c_channel));

  device->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
  device->DdkRelease();
}

}  // namespace ssd1306
