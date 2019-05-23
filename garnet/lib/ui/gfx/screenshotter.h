// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
#define GARNET_LIB_UI_GFX_SCREENSHOTTER_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

#include <string>

#include "garnet/lib/ui/gfx/engine/engine.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/lib/escher/vk/buffer.h"

namespace scenic_impl {
namespace gfx {

class Screenshotter {
 public:
  static void TakeScreenshot(
      Engine* engine,
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback callback);

 private:
  static void OnCommandBufferDone(
      const escher::BufferPtr& buffer, uint32_t width, uint32_t height,
      uint32_t rotation,
      fuchsia::ui::scenic::Scenic::TakeScreenshotCallback done_callback);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_SCREENSHOTTER_H_
