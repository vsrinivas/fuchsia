// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/cts/experimental/cpp/fidl.h>

#include <zxtest/zxtest.h>

namespace {

class ExperimentalCtsTest : public zxtest::Test {
 public:
  ~ExperimentalCtsTest() override = default;
  void SetUp() override {}
};

TEST_F(ExperimentalCtsTest, FidlTest) {
  ASSERT_EQ(fuchsia::cts::experimental::HELLO, "hello");
  ASSERT_EQ(fuchsia::cts::experimental::WORLD, "world");
}

}  // namespace
