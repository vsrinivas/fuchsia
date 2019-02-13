// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_
#define GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/sys/cpp/startup_context.h>

namespace fuchsia {
namespace feedback {

// Provides data useful to attach in feedback reports (crash or user feedback).
class FeedbackAgent : public DataProvider {
 public:
  FeedbackAgent(::sys::StartupContext* startup_context);

  // Returns a PNG image of the current view.
  void GetPngScreenshot(GetPngScreenshotCallback callback) override;

 private:
  // Connects to Scenic and sets up the error handler in case we lose the
  // connection.
  void ConnectToScenic();

  // Signals to all the pending GetPngScreenshot callbacks that an error
  // occurred, most likely the loss of the connection with Scenic.
  void TerminateAllGetPngScreenshotCallbacks();

  ::sys::StartupContext* context_;

  fuchsia::ui::scenic::ScenicPtr scenic_;
  bool is_connected_to_scenic_;
  // We keep track of the pending GetPngScreenshot callbacks so we can terminate
  // all of them when we lose the connection with Scenic.
  std::vector<std::unique_ptr<GetPngScreenshotCallback>>
      get_png_screenshot_callbacks_;
};

}  // namespace feedback
}  // namespace fuchsia

#endif  // GARNET_BIN_FEEDBACK_DATA_FEEDBACK_AGENT_H_
