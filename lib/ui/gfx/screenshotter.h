// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
#define GARNET_LIB_UI_GFX_SCREENSHOTTER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <string>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "lib/escher/vk/image.h"
#include "lib/fxl/macros.h"

namespace scenic {
namespace gfx {

class Screenshotter {
 public:
  explicit Screenshotter(Engine* engine) : engine_(engine) {}

  void TakeScreenshot(
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback);

 private:
  static void OnCommandBufferDone(
      const escher::ImagePtr& image, uint32_t width, uint32_t height,
      vk::Device device,
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback);

  Engine* const engine_;  // Not owned.
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
