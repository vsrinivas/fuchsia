// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "apps/mozart/services/views/cpp/formatting.h"
#include "apps/mozart/services/views/interfaces/view_trees.mojom.h"
#include "apps/mozart/src/view_manager/view_container_state.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace view_manager {

class ViewRegistry;
class ViewState;
class ViewStub;
class ViewTreeImpl;

// Describes the state of a particular view tree.
// This object is owned by the ViewRegistry that created it.
class ViewTreeState : public ViewContainerState {
 public:
  enum {
    // Some of the tree's views have been invalidated.
    INVALIDATION_VIEWS_INVALIDATED = 1u << 0,

    // The renderer changed.
    INVALIDATION_RENDERER_CHANGED = 1u << 1,
  };

  ViewTreeState(ViewRegistry* registry,
                mojo::ui::ViewTreeTokenPtr view_tree_token,
                mojo::InterfaceRequest<mojo::ui::ViewTree> view_tree_request,
                mojo::ui::ViewTreeListenerPtr view_tree_listener,
                const std::string& label);
  ~ViewTreeState() override;

  ftl::WeakPtr<ViewTreeState> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Gets the token used to refer to this view tree globally.
  // Caller does not obtain ownership of the token.
  const mojo::ui::ViewTreeTokenPtr& view_tree_token() const {
    return view_tree_token_;
  }

  // Gets the view tree listener interface, never null.
  // Caller does not obtain ownership of the view tree listener.
  const mojo::ui::ViewTreeListenerPtr& view_tree_listener() const {
    return view_tree_listener_;
  }

  // The view tree's renderer.
  const mojo::gfx::composition::RendererPtr& renderer() const {
    return renderer_;
  }
  void SetRenderer(mojo::gfx::composition::RendererPtr renderer);

  // The view tree's frame scheduler.
  // This is updated whenever the renderer is changed.
  const mojo::gfx::composition::FrameSchedulerPtr& frame_scheduler() const {
    return frame_scheduler_;
  }

  // Gets the view tree's root view.
  ViewStub* GetRoot() const;

  // Starts tracking a hit tester request.
  // The request will be satisfied by the current renderer if possible.
  // The callback will be invoked when the renderer changes.
  void RequestHitTester(
      mojo::InterfaceRequest<mojo::gfx::composition::HitTester>
          hit_tester_request,
      const mojo::ui::ViewInspector::GetHitTesterCallback& callback);

  // Gets or sets flags describing the invalidation state of the view tree.
  uint32_t invalidation_flags() const { return invalidation_flags_; }
  void set_invalidation_flags(uint32_t value) { invalidation_flags_ = value; }

  // Gets or sets whether a frame has been scheduled with the renderer
  // to handle invalidations.
  bool frame_scheduled() const { return frame_scheduled_; }
  void set_frame_scheduled(bool value) { frame_scheduled_ = value; }

  ViewTreeState* AsViewTreeState() override;

  const std::string& label() const { return label_; }
  const std::string& FormattedLabel() const override;

 private:
  void ClearHitTesterCallbacks(bool renderer_changed);

  mojo::ui::ViewTreeTokenPtr view_tree_token_;
  mojo::ui::ViewTreeListenerPtr view_tree_listener_;

  const std::string label_;
  mutable std::string formatted_label_cache_;

  std::unique_ptr<ViewTreeImpl> impl_;
  mojo::Binding<mojo::ui::ViewTree> view_tree_binding_;

  mojo::gfx::composition::RendererPtr renderer_;
  mojo::gfx::composition::FrameSchedulerPtr frame_scheduler_;

  std::vector<mojo::ui::ViewInspector::GetHitTesterCallback>
      pending_hit_tester_callbacks_;

  uint32_t invalidation_flags_ = 0u;
  bool frame_scheduled_ = false;

  ftl::WeakPtrFactory<ViewTreeState> weak_factory_;  // must be last

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewTreeState);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_
