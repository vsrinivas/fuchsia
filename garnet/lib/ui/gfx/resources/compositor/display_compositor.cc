// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"

#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/swapchain/display_swapchain.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo DisplayCompositor::kTypeInfo = {
    ResourceType::kCompositor | ResourceType::kDisplayCompositor, "DisplayCompositor"};

DisplayCompositor::DisplayCompositor(Session* session, ResourceId id, SceneGraphWeakPtr scene_graph,
                                     Display* display,
                                     std::unique_ptr<DisplaySwapchain> display_swapchain)
    : Compositor(session, id, DisplayCompositor::kTypeInfo, std::move(scene_graph),
                 std::move(display_swapchain)) {
  FXL_CHECK(display);
  static_cast<DisplaySwapchain*>(this->swapchain())
      ->RegisterVsyncListener([display](zx::time timestamp) { display->OnVsync(timestamp); });
}

}  // namespace gfx
}  // namespace scenic_impl
