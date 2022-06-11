// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim3-mcu.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace stm {
TEST(Vim3McuTest, FanLevel) {
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0x88, 1}).ExpectWriteStop({0x21, 0x03});

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
  EXPECT_TRUE(endpoints.is_ok());

  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), &mock_i2c);
  EXPECT_OK(loop.StartThread());

  ddk::I2cChannel i2c(std::move(endpoints->client));
  StmMcu device(nullptr, std::move(i2c));
  device.Init();
  mock_i2c.VerifyAndClear();
}
}  // namespace stm
