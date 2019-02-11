// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "feedback_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>

namespace fuchsia {
namespace feedback {

void FeedbackAgent::GetPngScreenshot(GetPngScreenshotCallback callback) {
  callback(Status::UNIMPLEMENTED, /*screenshot=*/nullptr);
}

}  // namespace feedback
}  // namespace fuchsia
