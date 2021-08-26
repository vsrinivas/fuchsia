// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <vector>

#include "src/ui/scenic/lib/gfx/engine/view_focuser_registry.h"
#include "src/ui/scenic/lib/gfx/engine/view_tree.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/display_compositor.h"

namespace scenic_impl::gfx {

class SceneGraph;
using SceneGraphWeakPtr = fxl::WeakPtr<SceneGraph>;

// Function for requesting focus transfers to view of ViewRef koid |request| on the authority of
// |requestor|. Return true if focus was transferred, false if it wasn't.
using RequestFocusFunc = fit::function<bool(zx_koid_t requestor, zx_koid_t request)>;

// SceneGraph stores pointers to all the Compositors created with it as a constructor argument, but
// it does not hold ownership of them.
//
// Command processors update this tree.
class SceneGraph final : public ViewFocuserRegistry {
 public:
  explicit SceneGraph(RequestFocusFunc request_focus);

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

  //
  // Focus transfer functionality
  //

  // |ViewFocuserRegistry|
  // A command processor, such as GFX or Flatland, may forward a view focuser to be bound here.
  void RegisterViewFocuser(
      SessionId session_id,
      fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) override;

  // |ViewFocuserRegistry|
  // Session cleanup terminates a view focuser connection and removes the binding.
  void UnregisterViewFocuser(SessionId session_id) override;

 private:
  // A small object that associates each Focuser request with a SessionId.
  // Close of channel does not trigger object cleanup; instead, we rely on Session cleanup.
  class ViewFocuserEndpoint : public fuchsia::ui::views::Focuser {
   public:
    ViewFocuserEndpoint(fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser,
                        fit::function<void(fuchsia::ui::views::ViewRef, RequestFocusCallback)>
                            request_focus_handler);
    ViewFocuserEndpoint(ViewFocuserEndpoint&& original);  // Needed for emplace.

    // |fuchsia.ui.views.Focuser|
    void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback response) override;

   private:
    // Capture SceneGraph* and SessionId, no explicit pointer management.
    // Note that it does *not* capture |this|, so it's movable in the move constructor.
    fit::function<void(fuchsia::ui::views::ViewRef, RequestFocusCallback)> request_focus_handler_;
    fidl::Binding<fuchsia::ui::views::Focuser> endpoint_;
  };

  friend class Compositor;

  // SceneGraph notify us upon creation/destruction.
  void AddCompositor(const CompositorWeakPtr& compositor);
  void RemoveCompositor(const CompositorWeakPtr& compositor);

  //
  // Fields
  //

  std::vector<CompositorWeakPtr> compositors_;

  ViewTree view_tree_;

  const RequestFocusFunc request_focus_;

  // Lifetime of ViewFocuserEndpoint is tied to owning Session's lifetime.
  // An early disconnect of ViewFocuserEndpoint is okay.
  std::unordered_map<SessionId, ViewFocuserEndpoint> view_focuser_endpoints_;

  fxl::WeakPtrFactory<SceneGraph> weak_factory_;  // Must be last.
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_SCENE_GRAPH_H_
