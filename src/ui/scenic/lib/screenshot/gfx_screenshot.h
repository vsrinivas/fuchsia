// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_

#include <fuchsia/ui/composition/cpp/fidl.h>

#include "screenshot_manager.h"

using TakeGfxScreenshot =
    std::function<void(fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback)>;

namespace screenshot {

class GfxScreenshot : public fuchsia::ui::composition::Screenshot {
 public:
  GfxScreenshot(TakeGfxScreenshot take_gfx_screenshot,
                fit::function<void(GfxScreenshot*)> destroy_instance_function);
  ~GfxScreenshot() override;

  // |fuchsia::ui::composition::Screenshot|
  void Take(fuchsia::ui::composition::ScreenshotTakeRequest format, TakeCallback callback) override;

 private:
  TakeGfxScreenshot take_gfx_screenshot_;

  fit::function<void(GfxScreenshot*)> destroy_instance_function_;

  TakeCallback callback_ = nullptr;

  // Should be last.
  fxl::WeakPtrFactory<GfxScreenshot> weak_factory_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_GFX_SCREENSHOT_H_
