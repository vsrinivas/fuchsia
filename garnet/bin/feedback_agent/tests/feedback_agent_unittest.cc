// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/feedback_agent/feedback_agent.h"

#include <ostream>

#include <fuchsia/feedback/cpp/fidl.h>
#include <garnet/public/lib/fostr/fidl/fuchsia/feedback/formatting.h>
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
  Status out_status = Status::UNKNOWN;
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

// Pretty-prints status in gTest matchers instead of the default byte string in
// case of failed expectations.
void PrintTo(const Status status, std::ostream* os) { *os << status; }

}  // namespace feedback
}  // namespace fuchsia
