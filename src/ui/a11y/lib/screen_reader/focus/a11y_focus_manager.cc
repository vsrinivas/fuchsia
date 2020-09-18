// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <optional>

#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

A11yFocusManager::A11yFocusManager(AccessibilityFocusChainRequester* focus_chain_requester,
                                   AccessibilityFocusChainRegistry* registry,
                                   FocusHighlightManager* focus_highlight_manager,
                                   inspect::Node inspect_node)
    : focus_chain_requester_(focus_chain_requester),
      focus_highlight_manager_(focus_highlight_manager),
      weak_ptr_factory_(this),
      inspect_node_(std::move(inspect_node)),
      inspect_property_current_focus_koid_(
          inspect_node_.CreateUint(kCurrentlyFocusedKoidInspectNodeName, 0)),
      inspect_property_current_focus_node_id_(
          inspect_node_.CreateUint(kCurrentlyFocusedNodeIdInspectNodeName, 0)) {
  FX_DCHECK(registry);
  FX_DCHECK(focus_highlight_manager_);
  registry->Register(weak_ptr_factory_.GetWeakPtr());
}

A11yFocusManager::A11yFocusManager() : weak_ptr_factory_(this) {}

A11yFocusManager::~A11yFocusManager() = default;

std::optional<A11yFocusManager::A11yFocusInfo> A11yFocusManager::GetA11yFocus() {
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

void A11yFocusManager::SetA11yFocus(zx_koid_t koid, uint32_t node_id,
                                    SetA11yFocusCallback set_focus_callback) {
  if (koid == currently_focused_view_) {
    // Same view a11y focus change.
    focused_node_in_view_map_[koid] = node_id;
    UpdateHighlights();
    set_focus_callback(true);
    UpdateInspectProperties();
    return;
  }
  // Different view, a Focus Chain Update is necessary.
  focus_chain_requester_->ChangeFocusToView(
      koid, [this, koid, node_id, callback = std::move(set_focus_callback)](bool success) {
        if (!success) {
          callback(false);
        } else {
          // Update current a11y focus to the given viewref and node_id.
          focused_node_in_view_map_[koid] = node_id;
          currently_focused_view_ = koid;
          UpdateHighlights();
          UpdateInspectProperties();
          callback(true);
        }
      });
}

void A11yFocusManager::OnViewFocus(zx_koid_t view_ref_koid) {
  uint32_t newly_focused_node_id = kRootNodeId;
  if (focused_node_in_view_map_.find(view_ref_koid) != focused_node_in_view_map_.end()) {
    newly_focused_node_id = focused_node_in_view_map_[view_ref_koid];
  }

  currently_focused_view_ = view_ref_koid;
  focused_node_in_view_map_[currently_focused_view_] = newly_focused_node_id;
  UpdateHighlights();
  UpdateInspectProperties();
}

void A11yFocusManager::UpdateInspectProperties() {
  // It's possible that the inspector could attempt to read these properties
  // while we are updating them. By setting inspect_property_current_focus_koid_
  // to a nonsense value of UINT64_MAX prior to updating, we ensure that we can
  // recognize instances in which the inspector reads the properties during an
  // update.
  inspect_property_current_focus_koid_.Set(UINT64_MAX);
  inspect_property_current_focus_node_id_.Set(focused_node_in_view_map_[currently_focused_view_]);
  inspect_property_current_focus_koid_.Set(currently_focused_view_);
}

void A11yFocusManager::UpdateHighlights() {
  FocusHighlightManager::SemanticNodeIdentifier newly_focused_node;

  newly_focused_node.koid = currently_focused_view_;
  newly_focused_node.node_id = focused_node_in_view_map_[currently_focused_view_];

  focus_highlight_manager_->UpdateHighlight(newly_focused_node);
}

void A11yFocusManager::ClearA11yFocus() {
  currently_focused_view_ = ZX_KOID_INVALID;
  focus_highlight_manager_->ClearHighlight();
}

}  // namespace a11y
