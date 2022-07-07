// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_
#define SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/ui/scenic/lib/screenshot/flatland_screenshot.h"
#include "src/ui/scenic/lib/screenshot/gfx_screenshot.h"

namespace screenshot {

class ScreenshotManager {
 public:
  explicit ScreenshotManager(bool use_flatland);
  ~ScreenshotManager() = default;

  void CreateBinding(fidl::InterfaceRequest<fuchsia::ui::composition::Screenshot> request);

 private:
  const bool use_flatland_;

  fidl::BindingSet<fuchsia::ui::composition::Screenshot,
                   std::unique_ptr<fuchsia::ui::composition::Screenshot>>
      bindings_;
};

}  // namespace screenshot

#endif  // SRC_UI_SCENIC_LIB_SCREENSHOT_SCREENSHOT_MANAGER_H_
