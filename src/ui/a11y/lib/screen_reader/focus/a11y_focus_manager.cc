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

A11yFocusManager::A11yFocusManager(fuchsia::ui::views::FocuserPtr focuser)
    : focuser_(std::move(focuser)) {}

A11yFocusManager::~A11yFocusManager() {
  // Call Cancel() on all the WaitMethod() that still exist.
  for (auto& iterator : wait_map_) {
    iterator.second->Cancel();
  }
  wait_map_.clear();
}

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
  const auto koid_iterator = koid_to_viewref_map_.find(koid);
  if (koid_iterator == koid_to_viewref_map_.end()) {
    FX_LOGS(INFO) << "No ViewRef found for the Koid:" << koid;
    set_focus_callback(false);
    return;
  }

  if (koid != currently_focused_view_) {
    // Call the focuser to set focus to the given viewref.
    focuser_->RequestFocus(
        Clone(koid_iterator->second),
        [this, koid, node_id, callback = std::move(set_focus_callback)](auto result) {
          if (result.is_err()) {
            FX_LOGS(ERROR) << "Error requesting focus using fuchsia.ui.views.focuser.";
            callback(false);
          } else {
            // Update current a11y focus to the given viewref and node_id.
            focused_node_in_view_map_[koid] = node_id;
            currently_focused_view_ = koid;
            callback(true);
          }
        });
  } else {
    // Update node_id of the view.
    focused_node_in_view_map_[koid] = node_id;
    set_focus_callback(true);
  }
}

void A11yFocusManager::AddViewRef(fuchsia::ui::views::ViewRef view_ref) {
  zx_koid_t koid = GetKoid(view_ref);
  const auto koid_iterator = koid_to_viewref_map_.find(koid);
  if (koid_iterator == koid_to_viewref_map_.end()) {
    // Add an entry in the map for the given viewref.
    koid_to_viewref_map_[koid] = std::move(view_ref);

    // Set Root node as the default node which gets a11y focus.
    focused_node_in_view_map_[koid] = kRootNodeId;

    // Initialize signal handler for view_ref.
    InitializeWaitMethod(koid);
  }
  // Update currently_focused_view_.
  currently_focused_view_ = koid;
}

void A11yFocusManager::InitializeWaitMethod(zx_koid_t koid) {
  auto wait_ptr =
      std::make_unique<async::WaitMethod<A11yFocusManager, &A11yFocusManager::CleanUpRemovedView>>(
          this, koid_to_viewref_map_[koid].reference.get(), ZX_EVENTPAIR_PEER_CLOSED);
  FX_CHECK(wait_ptr->Begin(async_get_default_dispatcher()) == ZX_OK);
  wait_map_[koid] = std::move(wait_ptr);
}

void A11yFocusManager::CleanUpRemovedView(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                          zx_status_t status, const zx_packet_signal* signal) {
  zx_koid_t viewref_koid = GetHandleKoid(wait->object());

  // Reset currently_focused_view_ if view in a11y focus is deleted.
  if (currently_focused_view_ == viewref_koid) {
    currently_focused_view_ = ZX_KOID_INVALID;
  }

  // Delete entry from focused_node_in_view_map_;
  focused_node_in_view_map_.erase(viewref_koid);

  // Delete viewref entry from koid_to_viewref_map_.
  koid_to_viewref_map_.erase(viewref_koid);

  // Delete the wait object from the wait_list_.
  wait_map_.erase(viewref_koid);
}

}  // namespace a11y
