// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_

#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"

namespace scenic_impl {

namespace display {
class Display;
}

namespace gfx {

class DisplaySwapchain;

// DisplayCompositor is a Compositor that renders directly to the display.
class DisplayCompositor : public Compositor {
 public:
  static const ResourceTypeInfo kTypeInfo;

  DisplayCompositor(Session* session, SessionId session_id, ResourceId id,
                    SceneGraphWeakPtr scene_graph, display::Display* display,
                    std::unique_ptr<DisplaySwapchain> swapchain);

  ~DisplayCompositor() override = default;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayCompositor);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_
