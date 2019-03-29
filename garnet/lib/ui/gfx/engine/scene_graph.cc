// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/scene_graph.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

CompositorWeakPtr SceneGraph::GetCompositor(GlobalId compositor_id) const {
  for (const CompositorWeakPtr& compositor : compositors_) {
    if (compositor && compositor->global_id() == compositor_id) {
      return compositor;
    }
  }
  return Compositor::kNullWeakPtr;
}

SceneGraph::SceneGraph() : weak_factory_(this) {}

void SceneGraph::AddCompositor(const CompositorWeakPtr& compositor) {
  FXL_DCHECK(compositor);
  compositors_.push_back(compositor);
}

void SceneGraph::RemoveCompositor(const CompositorWeakPtr& compositor) {
  FXL_DCHECK(compositor);
  auto it = std::find_if(compositors_.begin(), compositors_.end(),
                         [compositor](const auto& c) -> bool {
                           return c.get() == compositor.get();
                         });
  FXL_DCHECK(it != compositors_.end());
  compositors_.erase(it);
}

}  // namespace gfx
}  // namespace scenic_impl
