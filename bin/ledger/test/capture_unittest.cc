// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/capture.h"

#include <memory>

#include "gtest/gtest.h"

namespace test {
namespace {

TEST(Capture, CaptureVariable) {
  int a1 = 0;
  std::string a2 = "";
  std::unique_ptr<std::string> a3;
  bool called = false;

  Capture([&called] { called = true; }, &a1, &a2, &a3)(
      1, "hello", std::make_unique<std::string>("world"));

  EXPECT_TRUE(called);
  EXPECT_EQ(1, a1);
  EXPECT_EQ("hello", a2);
  EXPECT_TRUE(a3);
  EXPECT_EQ("world", *a3);
}

}  // namespace test
}  // namespace
