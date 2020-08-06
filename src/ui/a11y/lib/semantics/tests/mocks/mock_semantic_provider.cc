// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdint>

#include "fuchsia/accessibility/semantics/cpp/fidl.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"

namespace accessibility_test {

fuchsia::ui::views::ViewRef MockSemanticProvider::CreateOrphanViewRef() {
  fuchsia::ui::views::ViewRef view_ref;

  FX_CHECK(zx::eventpair::create(0u, &view_ref.reference, &eventpair_peer_) == ZX_OK);
  return view_ref;
}

namespace {
fuchsia::ui::views::ViewRef Clone(const fuchsia::ui::views::ViewRef& view_ref) {
  fuchsia::ui::views::ViewRef clone;
  FX_CHECK(fidl::Clone(view_ref, &clone) == ZX_OK);
  return clone;
}

}  // namespace

MockSemanticProvider::MockSemanticProvider(
    fuchsia::accessibility::semantics::SemanticsManager* manager)
    : view_ref_(CreateOrphanViewRef()) {
  manager->RegisterViewForSemantics(Clone(view_ref_),
                                    semantic_listener_bindings_.AddBinding(&semantic_listener_),
                                    tree_ptr_.NewRequest());
  commit_failed_ = false;

  semantic_listener_.SetSliderValueActionCallback(
      [this](uint32_t node_id, fuchsia::accessibility::semantics::Action action) {
        fuchsia::accessibility::semantics::States state;
        state.set_range_value(slider_node_.states().range_value() + slider_delta_);
        slider_node_.set_states(std::move(state));

        std::vector<fuchsia::accessibility::semantics::Node> update_nodes;
        update_nodes.push_back(std::move(slider_node_));

        // Update the node created above.
        UpdateSemanticNodes(std::move(update_nodes));
        CommitUpdates();
      });
}

void MockSemanticProvider::UpdateSemanticNodes(
    std::vector<fuchsia::accessibility::semantics::Node> nodes) {
  tree_ptr_->UpdateSemanticNodes(std::move(nodes));
}

void MockSemanticProvider::DeleteSemanticNodes(std::vector<uint32_t> node_ids) {
  tree_ptr_->DeleteSemanticNodes(std::move(node_ids));
}

void MockSemanticProvider::CommitUpdates() {
  tree_ptr_->CommitUpdates([this]() { commit_failed_ = true; });
}

void MockSemanticProvider::SetHitTestResult(std::optional<uint32_t> hit_test_result) {
  semantic_listener_.SetHitTestResult(hit_test_result);
}

void MockSemanticProvider::SetSemanticsEnabled(bool enabled) {
  semantic_listener_.OnSemanticsModeChanged(enabled, []() {});
}

bool MockSemanticProvider::GetSemanticsEnabled() {
  return semantic_listener_.GetSemanticsEnabled();
}

void MockSemanticProvider::SendEventPairSignal() {
  // Reset event pair peer. This should call close on the event pair that will send peer closed
  // signal.
  eventpair_peer_.reset();
}

void MockSemanticProvider::SetRequestedAction(fuchsia::accessibility::semantics::Action action) {
  semantic_listener_.SetRequestedAction(action);
}

fuchsia::accessibility::semantics::Action MockSemanticProvider::GetRequestedAction() const {
  return semantic_listener_.GetRequestedAction();
}

uint32_t MockSemanticProvider::GetRequestedActionNodeId() const {
  return semantic_listener_.GetRequestedActionNodeId();
}

bool MockSemanticProvider::IsChannelClosed() { return !tree_ptr_.channel().is_valid(); }

void MockSemanticProvider::SetSliderDelta(uint32_t new_slider_delta) {
  slider_delta_ = new_slider_delta;
}

void MockSemanticProvider::SetSliderNode(fuchsia::accessibility::semantics::Node new_node) {
  slider_node_ = std::move(new_node);
}

void MockSemanticProvider::SetOnAccessibilityActionCallbackStatus(bool status) {
  return semantic_listener_.SetOnAccessibilityActionCallbackStatus(status);
}

bool MockSemanticProvider::OnAccessibilityActionRequestedCalled() const {
  return semantic_listener_.OnAccessibilityActionRequestedCalled();
}

}  // namespace accessibility_test
