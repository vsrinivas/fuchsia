// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/arraysize.h"
#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(ArraySize, Control) {
  int numbers[] = {1, 2, 3};
  EXPECT_EQ(3u, arraysize(numbers));
}

}  // namespace
}  // namespace fxl
