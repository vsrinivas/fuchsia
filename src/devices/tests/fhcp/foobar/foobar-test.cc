// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

namespace foobar {

class FoobarDriverTest : public zxtest::Test {};

TEST_F(FoobarDriverTest, Test1) { ASSERT_TRUE(true); }
}  // namespace foobar
