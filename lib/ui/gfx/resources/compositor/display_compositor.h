// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_
#define GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"

namespace scenic {
namespace gfx {

class Display;
class DisplaySwapchain;

// DisplayCompositor is a Compositor that renders directly to the display.
class DisplayCompositor : public Compositor {
 public:
  static const ResourceTypeInfo kTypeInfo;

  DisplayCompositor(Session* session, scenic::ResourceId id, Display* display,
                    std::unique_ptr<Swapchain> swapchain);

  ~DisplayCompositor() override;

  // | Resource |
  void Accept(class ResourceVisitor* visitor) override;

 private:
  Display* const display_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayCompositor);
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_COMPOSITOR_DISPLAY_COMPOSITOR_H_
