// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include "gtest/gtest.h"

namespace fidl {
namespace {

TEST(Table, IsEmpty) {
  test::misc::SimpleTable input;
  EXPECT_TRUE(input.IsEmpty());
  input.set_x(42);
  EXPECT_FALSE(input.IsEmpty());
}

}  // namespace
}  // namespace fidl
