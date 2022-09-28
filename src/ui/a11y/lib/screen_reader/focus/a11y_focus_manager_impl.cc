// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager_impl.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

A11yFocusManagerImpl::A11yFocusManagerImpl(AccessibilityFocusChainRequester* focus_chain_requester,
                                           AccessibilityFocusChainRegistry* registry,
                                           ViewSource* view_source,
                                           VirtualKeyboardManager* virtual_keyboard_manager,
                                           inspect::Node inspect_node)
    : focus_chain_requester_(focus_chain_requester),
      view_source_(view_source),
      virtual_keyboard_manager_(virtual_keyboard_manager),
      weak_ptr_factory_(this),
      inspect_node_(std::move(inspect_node)),
      inspect_property_current_focus_koid_(
          inspect_node_.CreateUint(kCurrentlyFocusedKoidInspectNodeName, 0)),
      inspect_property_current_focus_node_id_(
          inspect_node_.CreateUint(kCurrentlyFocusedNodeIdInspectNodeName, 0)) {
  FX_DCHECK(registry);
  registry->Register(weak_ptr_factory_.GetWeakPtr());
}

A11yFocusManagerImpl::~A11yFocusManagerImpl() { ClearHighlights(); }

std::optional<A11yFocusManager::A11yFocusInfo> A11yFocusManagerImpl::GetA11yFocus() {
  const auto iterator = focused_node_in_view_map_.find(currently_focused_view_);
  if (iterator == focused_node_in_view_map_.end()) {
    FX_LOGS(INFO) << "No view is currently in a11y-focus.";
    return std::nullopt;
  }
  A11yFocusInfo focus_info;
  focus_info.view_ref_koid = currently_focused_view_;
  focus_info.node_id = iterator->second;
  return focus_info;
}

void A11yFocusManagerImpl::SetA11yFocus(zx_koid_t koid, uint32_t node_id,
                                        SetA11yFocusCallback set_focus_callback) {
  // We don't want to request a focus chain update if we're transferring focus
  // within the same view OR the newly focused view contains a visible virtual
  // keyboard.
  if (koid == currently_focused_view_ ||
      virtual_keyboard_manager_->ViewHasVisibleVirtualkeyboard(koid)) {
    UpdateFocus(koid, node_id);
    set_focus_callback(true);
    return;
  }

  // Retrieve the view's ViewRef.
  auto view = view_source_->GetViewWrapper(koid);
  if (!view) {
    set_focus_callback(false);
    return;
  }
  auto view_ref = view->ViewRefClone();

  // Different view, a Focus Chain Update is necessary.
  focus_chain_requester_->ChangeFocusToView(
      std::move(view_ref),
      [this, koid, node_id, callback = std::move(set_focus_callback)](bool success) {
        if (!success) {
          callback(false);
        } else {
          // Update current a11y focus to the given viewref and node_id.
          UpdateFocus(koid, node_id);
          callback(true);
        }
      });
}

void A11yFocusManagerImpl::UpdateFocus(zx_koid_t newly_focused_view, uint32_t newly_focused_node) {
  // Update highlights BEFORE updating the focus state, because clearing the
  // old highlight requires the old focus state.
  UpdateHighlights(newly_focused_view, newly_focused_node);

  focused_node_in_view_map_[newly_focused_view] = newly_focused_node;
  currently_focused_view_ = newly_focused_view;

  if (on_a11y_focus_updated_callback_) {
    on_a11y_focus_updated_callback_(GetA11yFocus());
  }
  UpdateInspectProperties();
}

void A11yFocusManagerImpl::OnViewFocus(zx_koid_t view_ref_koid) {
  uint32_t newly_focused_node_id = kRootNodeId;
  if (focused_node_in_view_map_.find(view_ref_koid) != focused_node_in_view_map_.end()) {
    newly_focused_node_id = focused_node_in_view_map_[view_ref_koid];
  }

  UpdateFocus(view_ref_koid, newly_focused_node_id);
}

void A11yFocusManagerImpl::UpdateInspectProperties() {
  // It's possible that the inspector could attempt to read these properties
  // while we are updating them. By setting inspect_property_current_focus_koid_
  // to a nonsense value of UINT64_MAX prior to updating, we ensure that we can
  // recognize instances in which the inspector reads the properties during an
  // update.
  inspect_property_current_focus_koid_.Set(UINT64_MAX);
  inspect_property_current_focus_node_id_.Set(focused_node_in_view_map_[currently_focused_view_]);
  inspect_property_current_focus_koid_.Set(currently_focused_view_);
}

void A11yFocusManagerImpl::UpdateHighlights(zx_koid_t newly_focused_view,
                                            uint32_t newly_focused_node) {
  ClearHighlights();

  // If there's no view in focus, then there's no work to do.
  if (newly_focused_view == ZX_KOID_INVALID) {
    return;
  }

  auto view = view_source_->GetViewWrapper(newly_focused_view);

  // If the focused view no longer exists, then there's no work to do.
  if (!view) {
    return;
  }

  auto tree_weak_ptr = view->view_semantics()->GetTree();
  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "Invalid tree pointer for view " << newly_focused_view;
    return;
  }

  auto transform = tree_weak_ptr->GetNodeToRootTransform(newly_focused_node);
  if (!transform) {
    FX_LOGS(ERROR) << "Could not compute node-to-root transform for node: " << newly_focused_node;
    return;
  }

  auto annotated_node = tree_weak_ptr->GetNode(newly_focused_node);
  if (!annotated_node) {
    FX_LOGS(ERROR) << "No node found with id: " << newly_focused_node;
    return;
  }

  auto bounding_box = annotated_node->location();

  // Request to draw the highlight.
  view->annotation_view()->DrawHighlight(bounding_box, transform->scale_vector(),
                                         transform->translation_vector());
}

void A11yFocusManagerImpl::ClearA11yFocus() {
  ClearHighlights();

  currently_focused_view_ = ZX_KOID_INVALID;
  if (on_a11y_focus_updated_callback_) {
    on_a11y_focus_updated_callback_(GetA11yFocus());
  }
}

void A11yFocusManagerImpl::ClearHighlights() {
  // If there's no view in focus, then there's no work to do.
  if (currently_focused_view_ == ZX_KOID_INVALID) {
    return;
  }

  auto view = view_source_->GetViewWrapper(currently_focused_view_);

  // If the focused view no longer exists, then there's no work to do.
  if (!view) {
    return;
  }

  view->annotation_view()->ClearFocusHighlights();
}

}  // namespace a11y
