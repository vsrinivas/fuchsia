// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"

#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo DisplayCompositor::kTypeInfo = {
    ResourceType::kCompositor | ResourceType::kDisplayCompositor, "DisplayCompositor"};

DisplayCompositor::DisplayCompositor(Session* session, SessionId session_id, ResourceId id,
                                     SceneGraphWeakPtr scene_graph, display::Display* display,
                                     std::unique_ptr<DisplaySwapchain> display_swapchain)
    : Compositor(session, session_id, id, DisplayCompositor::kTypeInfo, std::move(scene_graph),
                 std::move(display_swapchain)) {
  FXL_CHECK(display);
  static_cast<DisplaySwapchain*>(this->swapchain())
      ->RegisterVsyncListener([display](zx::time timestamp) { display->OnVsync(timestamp); });
}

}  // namespace gfx
}  // namespace scenic_impl
