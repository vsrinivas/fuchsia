// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>

#include <zxtest/zxtest.h>

#include "x86.h"

namespace x86 {

// Test that we can startup and cleaning shutdown the ACPICA library.
TEST(X86DeviceTest, BasicAcpica) {
  std::unique_ptr<X86> dev;
  ASSERT_OK(X86::Create(nullptr, driver_unit_test::GetParent(), &dev));
  ASSERT_OK(dev->EarlyAcpiInit());
}

}  // namespace x86
