// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/screenshot_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace screenshot {

ScreenshotManager::ScreenshotManager(bool use_flatland) : use_flatland_(use_flatland) {}

void ScreenshotManager::CreateBinding(
    fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request) {
  if (use_flatland_) {
    bindings_.AddBinding(std::make_unique<screenshot::FlatlandScreenshot>(), std::move(request));
  } else {
    bindings_.AddBinding(std::make_unique<screenshot::GfxScreenshot>(), std::move(request));
  }
}

}  // namespace screenshot
