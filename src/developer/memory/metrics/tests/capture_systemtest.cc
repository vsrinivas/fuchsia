// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"

namespace memory {
namespace test {

// These tests are exercising the real system services. As such we can't assume much about exactly
// what is running and what the memory looks like. We're just vetting whether they return any data
// at all without error.
using CaptureSystemTest = testing::Test;

TEST_F(CaptureSystemTest, KMEM) {
  CaptureState state;
  auto ret = Capture::GetCaptureState(&state);
  ASSERT_EQ(ZX_OK, ret);
  Capture c;
  ret = Capture::GetCapture(&c, state, KMEM);
  ASSERT_EQ(ZX_OK, ret);
  EXPECT_LT(0U, c.kmem().free_bytes);
  EXPECT_LT(0U, c.kmem().total_bytes);
}

TEST_F(CaptureSystemTest, VMO) {
  CaptureState state;
  auto ret = Capture::GetCaptureState(&state);
  ASSERT_EQ(ZX_OK, ret);
  Capture c;
  ret = Capture::GetCapture(&c, state, VMO);
  ASSERT_EQ(ZX_OK, ret);
  EXPECT_LT(0U, c.kmem().free_bytes);
  EXPECT_LT(0U, c.kmem().total_bytes);

  ASSERT_LT(0U, c.koid_to_process().size());
}

}  // namespace test
}  // namespace memory
