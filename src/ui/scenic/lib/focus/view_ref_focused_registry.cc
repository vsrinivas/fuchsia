// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/focus/view_ref_focused_registry.h"

#include <lib/syslog/cpp/macros.h>

namespace focus {

void ViewRefFocusedRegistry::Register(
    zx_koid_t view_ref_koid, fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> endpoint) {
  auto [_, inserted] = pending_requests_.try_emplace(view_ref_koid, std::move(endpoint));
  FX_DCHECK(inserted) << "endpoint emplace should always succeed";
}

void ViewRefFocusedRegistry::Update(const view_tree::Snapshot& snapshot) {
  // Remove the clients which are removed from the snapshot.
  for (auto it = endpoints_.begin(); it != endpoints_.end();) {
    const zx_koid_t koid = it->first;
    if (snapshot.view_tree.count(koid) == 0 && snapshot.unconnected_views.count(koid) == 0) {
      it = endpoints_.erase(it);
    } else {
      ++it;
    }
  }

  // Register any pending clients which are added to the snapshot.
  for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
    const zx_koid_t koid = it->first;
    if (snapshot.view_tree.count(koid) > 0) {
      auto [_, inserted] = endpoints_.emplace(koid, Endpoint(std::move(it->second)));
      FX_DCHECK(inserted) << "endpoint emplace should always succeed";
      it = pending_requests_.erase(it);
    } else {
      ++it;
    }
  }
}

void ViewRefFocusedRegistry::UpdateFocus(zx_koid_t old_focus, zx_koid_t new_focus) {
  FX_DCHECK(old_focus != new_focus) << "invariant";
  if (endpoints_.count(old_focus) > 0) {
    endpoints_.at(old_focus).UpdateFocus(false);
  } else {
    FX_DLOGS(INFO) << "Client lost focus, but cannot be notified. View ref koid: " << old_focus;
  }

  if (endpoints_.count(new_focus) > 0) {
    endpoints_.at(new_focus).UpdateFocus(true);
  } else {
    FX_DLOGS(INFO) << "Client gained focus, but cannot be notified. View ref koid:" << new_focus;
  }
}

ViewRefFocusedRegistry::Endpoint::Endpoint(
    fidl::InterfaceRequest<fuchsia::ui::views::ViewRefFocused> endpoint)
    : endpoint_(this, std::move(endpoint)) {}

ViewRefFocusedRegistry::Endpoint::Endpoint(Endpoint&& original) noexcept
    : focused_state_(std::move(original.focused_state_)),
      response_(std::move(original.response_)),
      endpoint_(this, original.endpoint_.Unbind()) {}

void ViewRefFocusedRegistry::Endpoint::Watch(
    fuchsia::ui::views::ViewRefFocused::WatchCallback callback) {
  FX_DCHECK(!response_) << "precondition";

  if (focused_state_) {
    // drain and reset
    fuchsia::ui::views::FocusState state;
    state.set_focused(focused_state_.value());
    callback(std::move(state));
    focused_state_.reset();
  } else {
    // Nothing to report yet. Stash the callback for later.
    response_ = std::move(callback);
  }

  FX_DCHECK(!focused_state_) << "postcondition";
}

void ViewRefFocusedRegistry::Endpoint::UpdateFocus(bool focused) {
  if (response_) {
    // drain and reset
    fuchsia::ui::views::FocusState state;
    state.set_focused(focused);
    response_(std::move(state));
    response_ = nullptr;
    focused_state_.reset();
  } else {
    // accumulate
    focused_state_ = std::optional<bool>{focused};
  }

  FX_DCHECK(!response_) << "postcondition";
}

}  // namespace focus
