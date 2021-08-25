// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <vector>

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"

namespace scenic_impl::gfx {

class SceneGraph;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// SceneGraph stores pointers to all the Compositors created with it as a constructor argument, but
// it does not hold ownership of them.
//
// Command processors update this tree.
class SceneGraph final {
 public:
  explicit SceneGraph();

  SceneGraphWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  //
  // Compositor functions
  //

  const std::vector<CompositorWeakPtr>& compositors() const { return compositors_; }

  // Returns the first compositor from the current compositors,  or invalid
  // WeakPtr if there are no compositors.
  // TODO(fxbug.dev/24376): get rid of SceneGraph::first_compositor().
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

  void OnNewFocusedView(zx_koid_t old_focus, zx_koid_t new_focus);

  //
  // View tree functions
  //

  // Expose view tree in a read-only manner.
  // NOTE: Modifications are handled exclusively by SceneGraph, for correct dispatch of FIDL events.
  const ViewTree& view_tree() const { return view_tree_; }

  // Invalidate the add_annotation_view_holder callback associated with koid.
  // Post: if koid is a valid RefNode, koid.add_annotation_view_holder is nullptr
  // TODO(fxbug.dev/59407): Disentangle the annotation logic from ViewTree.
  void InvalidateAnnotationViewHolder(zx_koid_t koid);

  // Tree topolocy: Apply all enqueued updates to the view tree in a transactional step.
  // Post: view_tree_ updated
  void ProcessViewTreeUpdates(ViewTreeUpdates view_tree_updates);

 private:
  friend class Compositor;

  // SceneGraph notify us upon creation/destruction.
  void AddCompositor(const CompositorWeakPtr& compositor);
  void RemoveCompositor(const CompositorWeakPtr& compositor);

  //
  // Fields
  //

  std::vector<CompositorWeakPtr> compositors_;

  ViewTree view_tree_;

  fxl::WeakPtrFactory<SceneGraph> weak_factory_;  // Must be last.
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
