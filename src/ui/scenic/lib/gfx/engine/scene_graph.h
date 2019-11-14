// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_

#include <fuchsia/ui/focus/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"

namespace scenic_impl {
namespace gfx {

class SceneGraph;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// SceneGraph stores pointers to all the Compositors created with it as a constructor argument, but
// it does not hold ownership of them.
//
// SceneGraph is the source of truth for the tree of ViewRefs, from which a FocusChain is generated.
// Command processors update this tree, and the input system may read or modify the focus.
class SceneGraph : public fuchsia::ui::focus::FocusChainListenerRegistry {
 public:
  explicit SceneGraph(sys::ComponentContext* app_context);

  SceneGraphWeakPtr GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

  //
  // Compositor functions
  //

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

  //
  // View tree and focus chain functions
  //

  // Expose view tree in a read-only manner.
  // NOTE: Modifications are handled exclusively by SceneGraph, for correct dispatch of FIDL events.
  const ViewTree& view_tree() const { return view_tree_; }

  // Tree topology: Enqueue transactional updates to the view tree, but do not apply them yet.
  // Invariant: view_tree_ not modified
  // Post: view_tree_updates_ grows by one
  void StageViewTreeUpdates(ViewTreeUpdates updates);

  // Tree topolocy: Apply all enqueued updates to the view tree in a transactional step.
  // Post: view_tree_ updated
  // Post: view_tree_updates_ cleared
  void ProcessViewTreeUpdates();

  // Focus chain: Adjust focus in the view tree.
  // Return kAccept if request was honored; otherwise return an error enum.
  // Invariant: view_tree_ not modified
  ViewTree::FocusChangeStatus RequestFocusChange(zx_koid_t requestor, zx_koid_t request);

  // |fuchsia.ui.focus.FocusChainListenerRegistry|
  void Register(fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> focus_chain_listener);

 private:
  friend class Compositor;

  // SceneGraph notify us upon creation/destruction.
  void AddCompositor(const CompositorWeakPtr& compositor);
  void RemoveCompositor(const CompositorWeakPtr& compositor);

  // If the focus chain has changed, (1) dispatch an updated focus chain to the FocusChainListener,
  // and (2) dispatch a FocusEvent to the clients that have gained and lost focus.
  void MaybeDispatchFidlFocusChainAndFocusEvents(const std::vector<zx_koid_t>& old_focus_chain);

  std::vector<CompositorWeakPtr> compositors_;

  ViewTree view_tree_;
  ViewTreeUpdates view_tree_updates_;
  fidl::Binding<fuchsia::ui::focus::FocusChainListenerRegistry> focus_chain_listener_registry_;
  fuchsia::ui::focus::FocusChainListenerPtr focus_chain_listener_;

  fxl::WeakPtrFactory<SceneGraph> weak_factory_;  // Must be last.
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
