// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_
#define GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_

#include <memory>

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"
#include "src/ui/lib/escher/escher.h"

namespace scenic_impl {
namespace gfx {

class DisplayManager;
class EventTimestamper;

class SwapchainFactory {
 public:
  // Create a swapchain for the specified display.  The display must not
  // already be claimed by another swapchain.
  static std::unique_ptr<DisplaySwapchain> CreateDisplaySwapchain(
      Display* display, DisplayManager* display_manager,
      EventTimestamper* event_timestamper, escher::Escher* escher);

 private:
  SwapchainFactory() = delete;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_
