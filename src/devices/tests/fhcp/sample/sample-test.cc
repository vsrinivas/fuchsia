// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

namespace sample {

class SampleDriverTest : public zxtest::Test {};

TEST_F(SampleDriverTest, Test1) { ASSERT_TRUE(true); }

TEST_F(SampleDriverTest, Test2) { ASSERT_TRUE(true); }

TEST_F(SampleDriverTest, Test3) { ASSERT_TRUE(true); }
}  // namespace sample
