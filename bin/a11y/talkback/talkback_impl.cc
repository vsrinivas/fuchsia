// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/talkback/talkback_impl.h"

namespace talkback {

TalkbackImpl::TalkbackImpl(component::StartupContext* startup_context) {
  manager_.set_error_handler(
      [this]() { FXL_LOG(ERROR) << "Cannot connect to a11y manager"; });
  tts_.set_error_handler(
      [this]() { FXL_LOG(ERROR) << "Cannot connect to tts service"; });
  manager_.events().OnNodeAction =
      fit::bind_member(this, &TalkbackImpl::OnNodeAction);
  startup_context->ConnectToEnvironmentService(manager_.NewRequest());
  startup_context->ConnectToEnvironmentService(tts_.NewRequest());
}

void TalkbackImpl::SetAccessibilityFocus(
    fuchsia::ui::viewsv1::ViewTreeToken token,
    fuchsia::ui::input::PointerEvent event) {
  manager_->GetHitAccessibilityNode(
      std::move(token), std::move(event),
      fit::bind_member(this, &TalkbackImpl::OnHitAccessibilityNodeCallback));
}

void TalkbackImpl::TapAccessibilityFocusedNode() {
  manager_->PerformAccessibilityAction(fuchsia::accessibility::Action::TAP);
}

void TalkbackImpl::OnNodeAction(int32_t view_id,
                                fuchsia::accessibility::Node node,
                                fuchsia::accessibility::Action action) {
  switch (action) {
    case fuchsia::accessibility::Action::GAIN_ACCESSIBILITY_FOCUS:
      tts_->Say(node.data.label, 0, [](uint64_t token) {});
      break;
    default:
      break;
  }
}

void TalkbackImpl::OnHitAccessibilityNodeCallback(
    int32_t view_id, fuchsia::accessibility::NodePtr node_ptr) {
  if (node_ptr == nullptr ||
      (focused_view_id_ == view_id && focused_node_id_ == node_ptr->node_id)) {
    return;
  }
  focused_view_id_ = view_id;
  focused_node_id_ = node_ptr->node_id;
  manager_->SetAccessibilityFocus(view_id, node_ptr->node_id);
}

}  // namespace talkback
