// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/swapchain/swapchain_factory.h"

#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"
#include "garnet/lib/ui/gfx/swapchain/vulkan_display_swapchain.h"

namespace scenic_impl {
namespace gfx {

std::unique_ptr<Swapchain> SwapchainFactory::CreateDisplaySwapchain(
    Display* display, DisplayManager* display_manager,
    EventTimestamper* event_timestamper, escher::Escher* escher) {
  FXL_DCHECK(!display->is_claimed());
#if SCENIC_VULKAN_SWAPCHAIN
  return std::make_unique<VulkanDisplaySwapchain>(display, event_timestamper,
                                                  escher);
#else
  return std::make_unique<DisplaySwapchain>(display_manager, display,
                                            event_timestamper, escher);
#endif
}

}  // namespace gfx
}  // namespace scenic_impl
