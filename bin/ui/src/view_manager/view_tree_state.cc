// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/view_manager/view_tree_state.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "services/ui/view_manager/view_registry.h"
#include "services/ui/view_manager/view_state.h"
#include "services/ui/view_manager/view_stub.h"
#include "services/ui/view_manager/view_tree_impl.h"

namespace view_manager {

ViewTreeState::ViewTreeState(
    ViewRegistry* registry,
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    mojo::InterfaceRequest<mojo::ui::ViewTree> view_tree_request,
    mojo::ui::ViewTreeListenerPtr view_tree_listener,
    const std::string& label)
    : view_tree_token_(view_tree_token.Pass()),
      view_tree_listener_(view_tree_listener.Pass()),
      label_(label),
      impl_(new ViewTreeImpl(registry, this)),
      view_tree_binding_(impl_.get(), view_tree_request.Pass()),
      weak_factory_(this) {
  DCHECK(view_tree_token_);
  DCHECK(view_tree_listener_);

  view_tree_binding_.set_connection_error_handler(
      base::Bind(&ViewRegistry::OnViewTreeDied, base::Unretained(registry),
                 base::Unretained(this), "ViewTree connection closed"));
  view_tree_listener_.set_connection_error_handler(
      base::Bind(&ViewRegistry::OnViewTreeDied, base::Unretained(registry),
                 base::Unretained(this), "ViewTreeListener connection closed"));
}

ViewTreeState::~ViewTreeState() {
  ClearHitTesterCallbacks(false /*renderer_changed*/);
}

void ViewTreeState::SetRenderer(mojo::gfx::composition::RendererPtr renderer) {
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
    mojo::InterfaceRequest<mojo::gfx::composition::HitTester>
        hit_tester_request,
    const mojo::ui::ViewInspector::GetHitTesterCallback& callback) {
  DCHECK(hit_tester_request.is_pending());
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
        label_.empty() ? base::StringPrintf("<T%d>", view_tree_token_->value)
                       : base::StringPrintf("<T%d:%s>", view_tree_token_->value,
                                            label_.c_str());
  }
  return formatted_label_cache_;
}

}  // namespace view_manager
