// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <stdlib.h>

#include <zxtest/zxtest.h>

namespace sysmem_driver {
namespace {

TEST(Device, OverrideCommandLine) {
  const char* kCommandLine = "test.device.commandline";
  setenv(kCommandLine, "5", 1);

  uint64_t value = 4096;

  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536, value);
  setenv(kCommandLine, "65537", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);

  // Trailing characters should cause the entire value to be ignored.
  setenv(kCommandLine, "65536a", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);

  // Empty values should be ignored.
  setenv(kCommandLine, "", 1);
  Device::OverrideSizeFromCommandLine(kCommandLine, &value);
  EXPECT_EQ(65536 * 2, value);
}

}  // namespace
}  // namespace sysmem_driver
