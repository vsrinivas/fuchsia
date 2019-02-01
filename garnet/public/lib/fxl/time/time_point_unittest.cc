// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/time/time_point.h"

#include "gtest/gtest.h"

namespace fxl {
namespace {

TEST(TimePoint, Control) {
  EXPECT_LT(TimePoint::Min(), TimePoint::Now());
  EXPECT_GT(TimePoint::Max(), TimePoint::Now());
}

}  // namespace
}  // namespace fxl
