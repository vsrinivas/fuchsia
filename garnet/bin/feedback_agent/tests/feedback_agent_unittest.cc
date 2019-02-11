// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/feedback_agent/feedback_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <gtest/gtest.h>

namespace fuchsia {
namespace feedback {
namespace {

// Unit-tests the implementation of the fuchsia.feedback.DataProvider FIDL
// interface.
//
// This does not test the environment service. It directly instantiates the
// class, without connecting through FIDL.
TEST(FeedbackAgentTest, GetPngScreenshot_Basic) {
  FeedbackAgent agent;
  Status out_status;
  std::unique_ptr<PngImage> out_image;
  agent.GetPngScreenshot([&out_status, &out_image](
                             Status status, std::unique_ptr<PngImage> image) {
    out_status = status;
    out_image = std::move(image);
  });
  EXPECT_EQ(out_status, Status::UNIMPLEMENTED);
  EXPECT_EQ(out_image, nullptr);
}

}  // namespace
}  // namespace feedback
}  // namespace fuchsia
