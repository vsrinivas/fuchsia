// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/callback/capture.h"

#include <memory>

#include <lib/fit/function.h>

#include "gtest/gtest.h"

namespace callback {
namespace {

TEST(Capture, CaptureVariable) {
  int a1 = 0;
  std::string a2;
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

TEST(Capture, CaptureConstReference) {
  int a1 = 0;
  int a2 = 0;
  bool called = false;

  fit::function<void(int, const int&)> capture =
      Capture([&called] { called = true; }, &a1, &a2);

  capture(1, 2);

  EXPECT_TRUE(called);
  EXPECT_EQ(1, a1);
  EXPECT_EQ(2, a2);
}

}  // namespace
}  // namespace callback
