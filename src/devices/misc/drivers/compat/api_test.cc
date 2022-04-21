// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/errors.h>

#include <gtest/gtest.h>

#include "lib/ddk/driver.h"
#include "src/devices/misc/drivers/compat/device.h"

TEST(ApiTest, GetVariableDfv2BufferTooSmall) {
  size_t size;
  zx_status_t status = device_get_variable(nullptr, compat::kDfv2Variable, nullptr, 0, &size);
  ASSERT_EQ(ZX_ERR_BUFFER_TOO_SMALL, status);
  ASSERT_EQ(2lu, size);
}

TEST(ApiTest, GetVariableDfv2) {
  char buf[2];
  size_t size;
  zx_status_t status = device_get_variable(nullptr, compat::kDfv2Variable, buf, sizeof(buf), &size);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(2lu, size);
  ASSERT_EQ('1', buf[0]);
  ASSERT_EQ(0, buf[1]);
}
