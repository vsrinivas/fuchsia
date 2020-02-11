// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>

#include <zxtest/zxtest.h>

#include "tpm.h"

namespace {

TEST(TpmCommandTestCase, GetRandom) {
  std::unique_ptr<tpm::Device> dev;
  ASSERT_OK(tpm::Device::Create(nullptr, driver_unit_test::GetParent(), &dev));
  ASSERT_OK(dev->Init());

  uint8_t buf[16] = {};
  size_t actual = 0;
  EXPECT_OK(dev->GetRandom(buf, sizeof(buf), &actual));
  EXPECT_EQ(actual, sizeof(buf));
  // It is vanishingly unlikely that 16 bytes of randomness came back as all
  // zeros.
  bool found_nonzero = false;
  for (uint8_t byte : buf) {
    if (byte) {
      found_nonzero = true;
      break;
    }
  }
  EXPECT_TRUE(found_nonzero);
  uint8_t out_state;
  EXPECT_OK(dev->Suspend(DEV_POWER_STATE_DCOLD, false, DEVICE_SUSPEND_REASON_POWEROFF, &out_state));
}

}  // namespace
