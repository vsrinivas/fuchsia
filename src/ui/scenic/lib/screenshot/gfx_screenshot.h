// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

namespace screenshot {

class GfxScreenshot : public fuchsia::ui::composition::Screenshot {
 public:
  GfxScreenshot();
  ~GfxScreenshot() override;

  // fuchsia::ui::composition::Screenshot
  void Take(fuchsia::ui::composition::ScreenshotTakeRequest format, TakeCallback callback) override;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_
