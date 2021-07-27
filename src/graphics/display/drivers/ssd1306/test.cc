// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-i2c/fake-i2c.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
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

TEST(Ssd1306Test, LifetimeTest) {
  FakeI2cParent parent;
  ddk::I2cChannel i2c_channel = ddk::I2cChannel(parent.GetProto());
  auto fake_parent = MockDevice::FakeRootParent();
  auto device = new Ssd1306(fake_parent.get());

  ASSERT_OK(device->Bind(i2c_channel));
  device_async_remove(device->zxdev());
  mock_ddk::ReleaseFlaggedDevices(fake_parent.get());

  // TODO(fxbug.dev/79639): Removed the obsolete fake_ddk.Ok() check.
  // To test Unbind and Release behavior, call UnbindOp and ReleaseOp directly.
}

}  // namespace ssd1306
