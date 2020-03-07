// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/default.h>

#include <optional>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/util/util.h"

namespace a11y {

A11yFocusManager::A11yFocusManager(AccessibilityFocusChainRequester* focus_chain_requester,
                                   AccessibilityFocusChainRegistry* registry)
    : focus_chain_requester_(focus_chain_requester), registry_(registry), weak_ptr_factory_(this) {
  FX_DCHECK(registry_);
  registry_->Register(weak_ptr_factory_.GetWeakPtr());
}

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
    set_focus_callback(true);
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
          callback(true);
        }
      });
}

void A11yFocusManager::OnViewFocus(zx_koid_t view_ref_koid) {
  currently_focused_view_ = view_ref_koid;
  const auto it = focused_node_in_view_map_.find(currently_focused_view_);
  if (it == focused_node_in_view_map_.end()) {
    focused_node_in_view_map_[currently_focused_view_] = kRootNodeId;
  }
}

}  // namespace a11y
