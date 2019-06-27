// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_SCENE_GRAPH_H_
#define GARNET_LIB_UI_GFX_ENGINE_SCENE_GRAPH_H_

#include <vector>

#include "garnet/lib/ui/gfx/resources/compositor/compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"

namespace scenic_impl {
namespace gfx {

class SceneGraph;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// SceneGraph stores pointers to all the Compositors created with it as a
// constructor argument, but it does not hold ownership of them.
class SceneGraph {
 public:
  SceneGraph();

  SceneGraphWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  const std::vector<CompositorWeakPtr>& compositors() const { return compositors_; }

  // Returns the first compositor from the current compositors,  or invalid
  // WeakPtr if there are no compositors.
  // TODO(SCN-1170): get rid of SceneGraph::first_compositor().
  CompositorWeakPtr first_compositor() const {
    for (auto& compositor : compositors_) {
      if (compositor) {
        return compositor;
      }
    }
    return Compositor::kNullWeakPtr;
  }

  // Returns the compositor requested, or nullptr if it does not exist.
  CompositorWeakPtr GetCompositor(GlobalId compositor_id) const;

 private:
  friend class Compositor;

  // SceneGraph notify us upon creation/destruction.
  void AddCompositor(const CompositorWeakPtr& compositor);
  void RemoveCompositor(const CompositorWeakPtr& compositor);

  std::vector<CompositorWeakPtr> compositors_;

  fxl::WeakPtrFactory<SceneGraph> weak_factory_;  // Must be last.
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_SCENE_GRAPH_H_
