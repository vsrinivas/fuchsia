// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_
#define GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_

#include <fuchsia/feedback/cpp/fidl.h>

namespace fuchsia {
namespace feedback {

// Provides data useful to attach in feedback reports (crash or user feedback).
class FeedbackAgent : public DataProvider {
 public:
  // Returns a PNG image of the current view.
  void GetPngScreenshot(GetPngScreenshotCallback callback) override;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_
