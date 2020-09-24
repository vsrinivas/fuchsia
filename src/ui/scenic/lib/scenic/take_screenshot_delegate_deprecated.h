// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_TAKE_SCREENSHOT_DELEGATE_DEPRECATED_H_
#define SRC_UI_SCENIC_LIB_SCENIC_TAKE_SCREENSHOT_DELEGATE_DEPRECATED_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

namespace scenic_impl {

// TODO(fxbug.dev/23901): Remove this and move the screenshot API out of Scenic.
class TakeScreenshotDelegateDeprecated {
 public:
  virtual ~TakeScreenshotDelegateDeprecated() = default;
  virtual void TakeScreenshot(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback) = 0;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_TAKE_SCREENSHOT_DELEGATE_DEPRECATED_H_
