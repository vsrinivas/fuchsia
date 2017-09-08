// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/compositor/display_compositor.h"

#include "garnet/bin/ui/scene_manager/displays/display.h"
#include "garnet/bin/ui/scene_manager/engine/display_swapchain.h"

namespace scene_manager {

const ResourceTypeInfo DisplayCompositor::kTypeInfo = {
    ResourceType::kCompositor | ResourceType::kDisplayCompositor,
    "DisplayCompositor"};

DisplayCompositor::DisplayCompositor(
    Session* session,
    scenic::ResourceId id,
    Display* display,
    std::unique_ptr<DisplaySwapchain> swapchain)
    : Compositor(session,
                 id,
                 DisplayCompositor::kTypeInfo,
                 std::move(swapchain)),
      display_(display) {
  FTL_DCHECK(display_);
}

DisplayCompositor::~DisplayCompositor() = default;

}  // namespace scene_manager
