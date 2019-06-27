// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/swapchain/swapchain_factory.h"

#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"

namespace scenic_impl {
namespace gfx {

std::unique_ptr<DisplaySwapchain> SwapchainFactory::CreateDisplaySwapchain(
    Display* display, DisplayManager* display_manager, EventTimestamper* event_timestamper,
    escher::Escher* escher) {
  FXL_DCHECK(!display->is_claimed());
  return std::make_unique<DisplaySwapchain>(display_manager, display, event_timestamper, escher);
}

}  // namespace gfx
}  // namespace scenic_impl
