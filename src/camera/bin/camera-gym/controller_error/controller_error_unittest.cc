// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/controller_error/controller_error.h"

#include <fuchsia/camera/gym/cpp/fidl.h>
#include <lib/fpromise/result.h>

#include <gtest/gtest.h>

namespace camera {

class ControllerErrorTest : public testing::Test {
 public:
  const std::string ErrorString(fuchsia::camera::gym::CommandError status) {
    return CommandErrorString(status);
  }
};

TEST_F(ControllerErrorTest, TestSingleCommandPassCases) {
  auto result = ErrorString(::fuchsia::camera::gym::CommandError::OUT_OF_RANGE);
  EXPECT_STREQ(result.c_str(), kCommandErrorOutOfRange);
}

}  // namespace camera
