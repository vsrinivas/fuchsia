// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/formatting/formatting.h"

#include <fuchsia/camera2/cpp/fidl.h>

#include <gtest/gtest.h>

namespace camera {
namespace {

TEST(Formatting, BasicFormat) {
  fuchsia::camera2::FrameRate fps{.frames_per_sec_numerator = 22, .frames_per_sec_denominator = 7};
  constexpr auto kExpected =
      "\"frames_per_sec_numerator\": \"22\"\n"
      "\"frames_per_sec_denominator\": \"7\"\n";
  auto string = ::camera::formatting::ToString(fps);
  EXPECT_EQ(string, kExpected);
  std::ostringstream oss;
  oss << fps;
  EXPECT_EQ(oss.str(), kExpected);
}

}  // namespace
}  // namespace camera
