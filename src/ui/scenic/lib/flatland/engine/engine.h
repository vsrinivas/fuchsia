// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_H_

// TODO(fxbug.dev/76640): delete when we delete hack_seen_display_ids_.
#include <set>

#include "src/ui/scenic/lib/flatland/default_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"
#include "src/ui/scenic/lib/flatland/flatland_manager.h"
#include "src/ui/scenic/lib/flatland/link_system.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace flatland {

// Engine is responsible for building a display list for DisplayCompositor, to insulate it from
// needing to know anything about the Flatland scene graph.
class Engine {
 public:
  Engine(std::shared_ptr<flatland::DisplayCompositor> flatland_compositor,
         std::shared_ptr<flatland::DefaultFlatlandPresenter> flatland_presenter,
         std::shared_ptr<flatland::UberStructSystem> uber_struct_system,
         std::shared_ptr<flatland::LinkSystem> link_system);
  ~Engine() = default;

  // Builds a display list for the Flatland content tree rooted at |display|.
  void RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                            const FlatlandDisplay& display,
                            scheduling::FrameRenderer::FramePresentedCallback callback);

  // Snapshots the current Flatland content tree rooted at |display| as a
  // view_tree::SubtreeSnapshot.
  view_tree::SubtreeSnapshot GenerateViewTreeSnapshot(const FlatlandDisplay& display) const;

 private:
  std::shared_ptr<flatland::DisplayCompositor> flatland_compositor_;
  std::shared_ptr<flatland::DefaultFlatlandPresenter> flatland_presenter_;
  std::shared_ptr<flatland::UberStructSystem> uber_struct_system_;
  std::shared_ptr<flatland::LinkSystem> link_system_;

  uint64_t last_rendered_frame_ = 0;

  // TODO(fxbug.dev/76640): hack so that we can call DisplayCompositor::AddDisplay() when we first
  // encounter a new display.  Need a more straightforward way to call AddDisplay().
  std::set<uint64_t> hack_seen_display_ids_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_ENGINE_H_
