// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/swapchain_factory.h"

#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

namespace scenic_impl {
namespace gfx {

std::unique_ptr<DisplaySwapchain> SwapchainFactory::CreateDisplaySwapchain(
    Display* display, Sysmem* sysmem, DisplayManager* display_manager, escher::Escher* escher) {
  FXL_DCHECK(!display->is_claimed());
  return std::make_unique<DisplaySwapchain>(sysmem, display_manager->default_display_controller(),
                                            display_manager->default_display_controller_listener(),
                                            display, escher);
}

}  // namespace gfx
}  // namespace scenic_impl
