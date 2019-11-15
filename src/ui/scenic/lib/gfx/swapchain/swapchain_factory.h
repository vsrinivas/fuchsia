// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_
#define SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_

#include <memory>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

namespace scenic_impl {
namespace gfx {

class DisplayManager;
class Sysmem;

class SwapchainFactory {
 public:
  // Create a swapchain for the specified display.  The display must not
  // already be claimed by another swapchain.
  static std::unique_ptr<DisplaySwapchain> CreateDisplaySwapchain(Display* display, Sysmem* sysmem,
                                                                  DisplayManager* display_manager,
                                                                  escher::Escher* escher);

 private:
  SwapchainFactory() = delete;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_SWAPCHAIN_FACTORY_H_
