// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_

#include <memory>
#include <string>

#include <fuchsia/ui/views/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/view_container_state.h"
#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "lib/ui/views/cpp/formatting.h"

namespace view_manager {

class ViewRegistry;
class ViewState;
class ViewStub;
class ViewTreeImpl;

// Describes the state of a particular view tree.
// This object is owned by the ViewRegistry that created it.
class ViewTreeState : public ViewContainerState {
 public:
  ViewTreeState(ViewRegistry* registry,
                ::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token,
                fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree>
                    view_tree_request,
                ::fuchsia::ui::viewsv1::ViewTreeListenerPtr view_tree_listener,
                fuchsia::ui::scenic::Scenic* scenic, const std::string& label);
  ~ViewTreeState() override;

  fxl::WeakPtr<ViewTreeState> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  // Gets the token used to refer to this view tree globally.
  ::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token() const {
    return view_tree_token_;
  }

  // Gets the view tree listener interface, never null.
  // Caller does not obtain ownership of the view tree listener.
  const ::fuchsia::ui::viewsv1::ViewTreeListenerPtr& view_tree_listener()
      const {
    return view_tree_listener_;
  }

  ViewTreeState* AsViewTreeState() override;

  const std::string& label() const { return label_; }
  const std::string& FormattedLabel() const override;

 private:
  ::fuchsia::ui::viewsv1::ViewTreeToken view_tree_token_;
  ::fuchsia::ui::viewsv1::ViewTreeListenerPtr view_tree_listener_;

  const std::string label_;
  mutable std::string formatted_label_cache_;

  std::unique_ptr<ViewTreeImpl> impl_;
  fidl::Binding<::fuchsia::ui::viewsv1::ViewTree> view_tree_binding_;

  fxl::WeakPtrFactory<ViewTreeState> weak_factory_;  // must be last

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewTreeState);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_TREE_STATE_H_
