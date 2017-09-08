// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_VIEW_TREE_STATE_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_VIEW_TREE_STATE_H_

#include <memory>
#include <string>

#include "lib/ui/views/cpp/formatting.h"
#include "lib/ui/views/fidl/view_trees.fidl.h"
#include "garnet/bin/ui/view_manager/internal/view_inspector.h"
#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

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
  };

  ViewTreeState(ViewRegistry* registry,
                mozart::ViewTreeTokenPtr view_tree_token,
                fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
                mozart::ViewTreeListenerPtr view_tree_listener,
                const std::string& label);
  ~ViewTreeState() override;

  ftl::WeakPtr<ViewTreeState> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Gets the token used to refer to this view tree globally.
  // Caller does not obtain ownership of the token.
  const mozart::ViewTreeTokenPtr& view_tree_token() const {
    return view_tree_token_;
  }

  // Gets the view tree listener interface, never null.
  // Caller does not obtain ownership of the view tree listener.
  const mozart::ViewTreeListenerPtr& view_tree_listener() const {
    return view_tree_listener_;
  }

  // Gets the view tree's root view.
  ViewStub* GetRoot() const;

  // Gets or sets flags describing the invalidation state of the view tree.
  uint32_t invalidation_flags() const { return invalidation_flags_; }
  void set_invalidation_flags(uint32_t value) { invalidation_flags_ = value; }

  ViewTreeState* AsViewTreeState() override;

  const std::string& label() const { return label_; }
  const std::string& FormattedLabel() const override;

  void RequestFocus(ViewStub* child_stub);
  const FocusChain* focus_chain();

 private:
  mozart::ViewTreeTokenPtr view_tree_token_;
  mozart::ViewTreeListenerPtr view_tree_listener_;

  const std::string label_;
  mutable std::string formatted_label_cache_;

  std::unique_ptr<ViewTreeImpl> impl_;
  fidl::Binding<mozart::ViewTree> view_tree_binding_;

  uint32_t invalidation_flags_ = 0u;

  ftl::WeakPtr<ViewStub> focused_view_;

  ftl::WeakPtrFactory<ViewTreeState> weak_factory_;  // must be last

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewTreeState);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_VIEW_TREE_STATE_H_
