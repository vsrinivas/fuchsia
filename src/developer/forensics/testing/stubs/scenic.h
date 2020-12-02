// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_STUBS_SCENIC_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_STUBS_SCENIC_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>

#include <cstdint>
#include <vector>

#include "src/developer/forensics/testing/stubs/fidl_server.h"

namespace forensics {
namespace stubs {

// Returns an empty screenshot, still needed when Scenic::TakeScreenshot() returns false as the FIDL
// ScreenshotData field is not marked optional in fuchsia.ui.scenic.Scenic.TakeScreenshot.
fuchsia::ui::scenic::ScreenshotData CreateEmptyScreenshot();

// Returns an 8-bit BGRA image of a |image_dim_in_px| x |image_dim_in_px| checkerboard, where each
// white/black region is a 10x10 pixel square.
fuchsia::ui::scenic::ScreenshotData CreateCheckerboardScreenshot(const uint32_t image_dim_in_px);

// Returns an empty screenshot with a pixel format different from BGRA-8.
fuchsia::ui::scenic::ScreenshotData CreateNonBGRA8Screenshot();

// Represents arguments for Scenic::TakeScreenshot().
struct TakeScreenshotResponse {
  fuchsia::ui::scenic::ScreenshotData screenshot;
  bool success;

  TakeScreenshotResponse(fuchsia::ui::scenic::ScreenshotData data, bool success)
      : screenshot(std::move(data)), success(success){};
};

using ScenicBase = MULTI_BINDING_STUB_FIDL_SERVER(fuchsia::ui::scenic, Scenic);

class Scenic : public ScenicBase {
 public:
  ~Scenic();

  // |fuchsia::ui::scenic::Scenic|.
  void TakeScreenshot(TakeScreenshotCallback callback) override;

  //  injection and verification methods.
  void set_take_screenshot_responses(std::vector<TakeScreenshotResponse> responses) {
    take_screenshot_responses_ = std::move(responses);
  }

 private:
  std::vector<TakeScreenshotResponse> take_screenshot_responses_;
};

class ScenicAlwaysReturnsFalse : public ScenicBase {
 public:
  // |fuchsia::ui::scenic::Scenic|.
  void TakeScreenshot(TakeScreenshotCallback callback) override;
};

class ScenicClosesConnection : public ScenicBase {
 public:
  // |fuchsia::ui::scenic::Scenic|.
  STUB_METHOD_CLOSES_ALL_CONNECTIONS(TakeScreenshot, TakeScreenshotCallback);
};

class ScenicNeverReturns : public ScenicBase {
 public:
  // |fuchsia::ui::scenic::Scenic|.
  STUB_METHOD_DOES_NOT_RETURN(TakeScreenshot, TakeScreenshotCallback);
};

}  // namespace stubs
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_STUBS_SCENIC_H_
