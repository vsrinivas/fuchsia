// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/functional/make_copyable.h"

#include <memory>

#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(MakeCopyableTest, Control) {
  std::function<void()> void_func = fxl::MakeCopyable([]() {});
  void_func();

  std::function<int()> int_func = fxl::MakeCopyable([]() { return 5; });
  EXPECT_EQ(5, int_func());

  std::unique_ptr<int> int_ptr(new int);
  *int_ptr = 42;
  std::function<int()> int_ptr_func =
      fxl::MakeCopyable([p = std::move(int_ptr)]() { return *p; });
  EXPECT_FALSE(int_ptr);
  EXPECT_EQ(42, int_ptr_func());
}

TEST(MakeCopyableTest, Mutable) {
  std::unique_ptr<int> src(new int);
  *src = 42;
  std::unique_ptr<int> dest;
  std::function<void()> mutable_func = fxl::MakeCopyable(
      [&dest, src = std::move(src) ]() mutable { dest = std::move(src); });
  mutable_func();
  EXPECT_EQ(42, *dest);
}

}  // namespace
}  // namespace fxl
