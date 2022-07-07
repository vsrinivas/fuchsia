// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/gfx_screenshot.h"

#include <lib/syslog/cpp/macros.h>

namespace screenshot {

GfxScreenshot::GfxScreenshot() = default;

GfxScreenshot::~GfxScreenshot() = default;

void GfxScreenshot::Take(fuchsia::ui::composition::ScreenshotTakeRequest format,
                         TakeCallback callback) {
  // TODO(fxbug.dev/103784): Add gfx hookup to easy screenshot protocol.
  FX_NOTIMPLEMENTED();
}

}  // namespace screenshot
