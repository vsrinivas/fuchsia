// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/flatland_screenshot.h"

#include <lib/syslog/cpp/macros.h>

namespace screenshot {

FlatlandScreenshot::FlatlandScreenshot() = default;

FlatlandScreenshot::~FlatlandScreenshot() = default;

void FlatlandScreenshot::Take(fuchsia::ui::composition::ScreenshotTakeRequest format,
                              TakeCallback callback) {
  // TODO(fxbug.dev/103741): Add flatland hookup to easy screenshot protocol.
  FX_NOTIMPLEMENTED();
}

}  // namespace screenshot
