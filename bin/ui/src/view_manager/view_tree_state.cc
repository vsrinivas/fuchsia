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
    mojo::InterfaceRequest<mozart::ViewTree> view_tree_request,
    mozart::ViewTreeListenerPtr view_tree_listener,
    const std::string& label)
    : view_tree_token_(view_tree_token.Pass()),
      view_tree_listener_(view_tree_listener.Pass()),
      label_(label),
      impl_(new ViewTreeImpl(registry, this)),
      view_tree_binding_(impl_.get(), view_tree_request.Pass()),
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

ViewTreeState::~ViewTreeState() {
  ClearHitTesterCallbacks(false /*renderer_changed*/);
}

void ViewTreeState::SetRenderer(mozart::RendererPtr renderer) {
  renderer_ = renderer.Pass();
  frame_scheduler_.reset();
  if (renderer_)
    renderer_->GetScheduler(GetProxy(&frame_scheduler_));

  ClearHitTesterCallbacks(true /*renderer_changed*/);
}

ViewStub* ViewTreeState::GetRoot() const {
  if (children().empty())
    return nullptr;
  return children().cbegin()->second.get();
}

void ViewTreeState::RequestHitTester(
    mojo::InterfaceRequest<mozart::HitTester> hit_tester_request,
    const mozart::ViewInspector::GetHitTesterCallback& callback) {
  FTL_DCHECK(hit_tester_request.is_pending());
  if (renderer_)
    renderer_->GetHitTester(hit_tester_request.Pass());
  pending_hit_tester_callbacks_.push_back(callback);
}

void ViewTreeState::ClearHitTesterCallbacks(bool renderer_changed) {
  for (const auto& callback : pending_hit_tester_callbacks_)
    callback.Run(renderer_changed);
  pending_hit_tester_callbacks_.clear();
}

ViewTreeState* ViewTreeState::AsViewTreeState() {
  return this;
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
