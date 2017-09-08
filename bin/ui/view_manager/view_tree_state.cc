// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_tree_state.h"

#include "apps/mozart/src/view_manager/view_registry.h"
#include "apps/mozart/src/view_manager/view_state.h"
#include "apps/mozart/src/view_manager/view_stub.h"
#include "apps/mozart/src/view_manager/view_tree_impl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace view_manager {

ViewTreeState::ViewTreeState(
    ViewRegistry* registry,
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
    mozart::ViewTreeListenerPtr view_tree_listener,
    const std::string& label)
    : view_tree_token_(std::move(view_tree_token)),
      view_tree_listener_(std::move(view_tree_listener)),
      label_(label),
      impl_(new ViewTreeImpl(registry, this)),
      view_tree_binding_(impl_.get(), std::move(view_tree_request)),
      weak_factory_(this) {
  FTL_DCHECK(view_tree_token_);
  FTL_DCHECK(view_tree_listener_);

  view_tree_binding_.set_connection_error_handler([this, registry] {
    registry->OnViewTreeDied(this, "ViewTree connection closed");
  });
  view_tree_listener_.set_connection_error_handler([this, registry] {
    registry->OnViewTreeDied(this, "ViewTreeListener connection closed");
  });
}

ViewTreeState::~ViewTreeState() {}

ViewStub* ViewTreeState::GetRoot() const {
  if (children().empty())
    return nullptr;
  return children().cbegin()->second.get();
}

ViewTreeState* ViewTreeState::AsViewTreeState() {
  return this;
}

void ViewTreeState::RequestFocus(ViewStub* child_stub) {
  if (child_stub->is_unavailable())
    return;
  focused_view_ = child_stub->GetWeakPtr();
}

const FocusChain* ViewTreeState::focus_chain() {
  return focused_view_ ? focused_view_->state()->focus_chain() : nullptr;
}

const std::string& ViewTreeState::FormattedLabel() const {
  if (formatted_label_cache_.empty()) {
    formatted_label_cache_ =
        label_.empty() ? ftl::StringPrintf("<T%d>", view_tree_token_->value)
                       : ftl::StringPrintf("<T%d:%s>", view_tree_token_->value,
                                           label_.c_str());
  }
  return formatted_label_cache_;
}

}  // namespace view_manager
