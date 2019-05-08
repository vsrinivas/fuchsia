// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
#define GARNET_LIB_UI_GFX_SCREENSHOTTER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <string>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/lib/fxl/macros.h"

namespace scenic_impl {
namespace gfx {

class Screenshotter {
 public:
  static void TakeScreenshot(
      Engine* engine,
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback);

 private:
  static void OnCommandBufferDone(
      const escher::ImagePtr& image, uint32_t width, uint32_t height,
      vk::Device device,
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
